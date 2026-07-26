#include "face_detector_helper.h"
#include "cam_helper.h"
#include "dl_image.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "human_face_detect_mnp01.hpp"
#include "human_face_detect_msr01.hpp"

#include <atomic>
#include <cstring>

static const char *TAG = "face_detector_helper";

#define FACE_DETECT_INTERVAL_MS 100 // AI 推理间隔 (ms)

static TaskHandle_t g_detector_task = NULL;
static SemaphoreHandle_t g_data_mutex = NULL;
static cam_subscriber_handle_t g_cam_sub_handle = NULL;

static face_detect_results_t g_latest_results = {0};
static std::atomic<bool> g_continuous_running{false};
static std::atomic<bool> g_is_inited{false};

// 只需要一个原始 RGB565 缓冲区！不要任何 RGB888 缓冲区！
static uint16_t *g_raw_rgb565_buf = nullptr;
static bool g_new_frame_ready = false;

static int g_src_w = 0;
static int g_src_h = 0;

// 使用两级检测器（参考例程）
static HumanFaceDetectMSR01 *g_msr01_detector = nullptr;
static HumanFaceDetectMNP01 *g_mnp01_detector = nullptr;

// 1. 摄像头回调：保存原始 RGB565 数据
static void on_camera_frame_cb(const camera_fb_t *fb, void *user_arg) {
  if (!g_continuous_running.load(std::memory_order_relaxed))
    return;
  if (!fb || !g_raw_rgb565_buf)
    return;

  // 🔴 将超时改为 0 (不等待)，避免卡死摄像头驱动任务
  if (xSemaphoreTake(g_data_mutex, 0) == pdTRUE) {
    if (fb->format == PIXFORMAT_RGB565 && fb->len <= (g_src_w * g_src_h * 2)) {
      memcpy(g_raw_rgb565_buf, fb->buf, fb->len);
      g_new_frame_ready = true;
    }
    xSemaphoreGive(g_data_mutex);
  }
}

// 2. 运行两级 AI 人脸检测（直接传入 uint16_t* RGB565）
static bool run_face_inference(uint16_t *rgb565_ptr, int width, int height,
                               face_detect_results_t *out_res) {
  if (!g_msr01_detector || !g_mnp01_detector || !rgb565_ptr || !out_res)
    return false;

  // 第一级：MSR01 候选框检测
  std::list<dl::detect::result_t> &candidates =
      g_msr01_detector->infer(rgb565_ptr, {height, width, 3});

  // 第二级：MNP01 精确检测与关键点提取
  std::list<dl::detect::result_t> &results =
      g_mnp01_detector->infer(rgb565_ptr, {height, width, 3}, candidates);

  out_res->count = 0;
  int idx = 0;

  for (auto &r : results) {
    if (idx >= FD_MAX_FACES)
      break;

    face_info_t &face = out_res->faces[idx];
    face.box[0] = r.box[0];
    face.box[1] = r.box[1];
    face.box[2] = r.box[2];
    face.box[3] = r.box[3];
    face.score = r.score;

    int kp_idx = 0;
    for (auto &k : r.keypoint) {
      if (kp_idx >= FD_MAX_KEYPOINTS)
        break;
      face.keypoint[kp_idx++] = (int)k;
    }
    face.keypoint_count = kp_idx;
    idx++;
  }

  out_res->count = idx;
  return true;
}

// 3. 结果绘制（画框与关键点）
static void draw_results_overlay(uint16_t *buf, int w, int h,
                                 const face_detect_results_t &r) {
  if (!buf || r.count <= 0)
    return;

  const uint16_t COLOR_BOX = 0xE007;
  const uint16_t COLOR_POINT = 0x00F8;

  for (int i = 0; i < r.count; i++) {
    const face_info_t &f = r.faces[i];

    // 🔴 严格限制在 0 ~ w-1 和 0 ~ h-1 范围内，防止写越界踩爆堆内存！
    int x1 = DL_MAX((int)f.box[0], 0);
    int y1 = DL_MAX((int)f.box[1], 0);
    int x2 = DL_MIN((int)f.box[2], w - 1);
    int y2 = DL_MIN((int)f.box[3], h - 1);

    if (x1 >= x2 || y1 >= y2)
      continue;

    dl::image::draw_hollow_rectangle(buf, h, w, x1, y1, x2, y2, COLOR_BOX);

    for (int k = 0; k < f.keypoint_count; k += 2) {
      int px = f.keypoint[k];
      int py = f.keypoint[k + 1];

      // 关键点也必须限制在屏幕有效范围内
      if (px >= 0 && px < w && py >= 0 && py < h) {
        dl::image::draw_point(buf, h, w, px, py, 4, COLOR_POINT);
      }
    }
  }
}

// 4. AI 后台任务
static void detector_worker_task(void *arg) {
  ESP_LOGI(TAG, "Face Detector Worker Started");

  while (1) {
    if (g_continuous_running.load(std::memory_order_relaxed)) {
      bool has_frame = false;
      face_detect_results_t temp_res = {0};

      // Hold semaphore for the entire check+inference+store cycle.
      // This prevents the camera callback from overwriting g_raw_rgb565_buf
      // while the AI is reading it, which would be a data race.
      if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (g_new_frame_ready) {
          g_new_frame_ready = false;
          has_frame = true;
        }

        if (has_frame) {
          run_face_inference(g_raw_rgb565_buf, g_src_w, g_src_h, &temp_res);
          g_latest_results = temp_res;
          if (g_latest_results.count > 0) {
            ESP_LOGI(TAG, ">>> Detected %d face(s)! <<<",
                     g_latest_results.count);
          }
        }
        xSemaphoreGive(g_data_mutex);
      }

      vTaskDelay(pdMS_TO_TICKS(FACE_DETECT_INTERVAL_MS));
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

extern "C" bool face_detector_helper_init(int fb_width, int fb_height) {
  if (g_is_inited.load())
    return true;

  g_src_w = fb_width;
  g_src_h = fb_height;

  size_t rgb565_size = fb_width * fb_height * sizeof(uint16_t);
  g_raw_rgb565_buf = (uint16_t *)heap_caps_malloc(
      rgb565_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (!g_raw_rgb565_buf) {
    ESP_LOGE(TAG, "Failed to allocate memory for RGB565 buffer");
    return false;
  }

  // 按照官方例程参数实例化两级检测器
  g_msr01_detector = new HumanFaceDetectMSR01(0.3F, 0.3F, 10, 0.3F);
  g_mnp01_detector = new HumanFaceDetectMNP01(0.4F, 0.3F, 10);

  g_data_mutex = xSemaphoreCreateBinary();
  xSemaphoreGive(g_data_mutex);  // Make available (binary sem starts at 0)
  g_continuous_running.store(false);

  xTaskCreatePinnedToCore(detector_worker_task, "fd_worker", 16 * 1024, NULL, 3,
                          &g_detector_task, 1);

  g_is_inited.store(true);
  ESP_LOGI(TAG, "Face detector initialized successfully");
  return true;
}

extern "C" void face_detector_helper_start_continuous(void) {
  if (!g_is_inited.load())
    return;

  if (!g_continuous_running.load()) {
    g_continuous_running.store(true);

    // 清空上次残留的检测结果，避免调用方读到旧缓存误判"0秒检测到人脸"
    if (g_data_mutex &&
        xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      memset(&g_latest_results, 0, sizeof(g_latest_results));
      g_new_frame_ready = false;
      xSemaphoreGive(g_data_mutex);
    }

    if (!g_cam_sub_handle) {
      g_cam_sub_handle = cam_helper_subscribe(on_camera_frame_cb, NULL);
    }
  }
}

extern "C" void face_detector_helper_stop_continuous(void) {
  if (!g_is_inited.load())
    return;

  if (g_continuous_running.load()) {
    g_continuous_running.store(false);
    if (g_cam_sub_handle) {
      cam_helper_unsubscribe(g_cam_sub_handle);
      g_cam_sub_handle = NULL;
    }
  }
}

extern "C" void face_detector_helper_deinit(void) {
  if (!g_is_inited.load())
    return;

  face_detector_helper_stop_continuous();

  if (g_detector_task) {
    vTaskDelete(g_detector_task);
    g_detector_task = NULL;
  }
  if (g_data_mutex) {
    vSemaphoreDelete(g_data_mutex);
    g_data_mutex = NULL;
  }
  if (g_raw_rgb565_buf)
    heap_caps_free(g_raw_rgb565_buf);

  delete g_msr01_detector;
  delete g_mnp01_detector;

  g_is_inited.store(false);
}

extern "C" bool face_detector_helper_is_running(void) {
  return g_continuous_running.load(std::memory_order_acquire);
}

extern "C" void face_detector_helper_get_results(face_detect_results_t *out) {
  if (!out)
    return;
  if (g_data_mutex &&
      xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(30)) == pdTRUE) {
    *out = g_latest_results;
    xSemaphoreGive(g_data_mutex);
  }
}

extern "C" bool face_detector_helper_get_latest_rgb565(uint16_t *dst_buf,
                                                       int width, int height) {
  if (!dst_buf || !g_raw_rgb565_buf || width != g_src_w || height != g_src_h)
    return false;

  // 🔴【核心修改】：超时时间设为 0！如果 AI Task 或摄像头正在占锁，直接返回
  // false，绝不卡死 LVGL 主线程！
  if (g_data_mutex && xSemaphoreTake(g_data_mutex, 0) == pdTRUE) {
    // 1. 拷贝原始画面
    memcpy(dst_buf, g_raw_rgb565_buf, g_src_w * g_src_h * sizeof(uint16_t));

    // 2. 叠加 AI 画框结果
    draw_results_overlay(dst_buf, g_src_w, g_src_h, g_latest_results);

    xSemaphoreGive(g_data_mutex);
    return true;
  }

  return false; // 拿不到锁直接跳过，防止 LVGL 线程死锁
}