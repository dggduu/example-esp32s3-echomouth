#include "face_detector_helper.h"
#include "cam_helper.h"
#include "dl_image.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "img_converters.h"

#include "human_face_detect_msr01.hpp"

#include <atomic>
#include <cstring>

static const char *TAG = "face_detector_helper";

// 控制推理周期（单位 ms），如 500ms = 2FPS，显著降低 CPU 负载
#define FACE_DETECT_INTERVAL_MS 500

static TaskHandle_t g_detector_task = NULL;
static SemaphoreHandle_t g_data_mutex = NULL;
static cam_subscriber_handle_t g_cam_sub_handle = NULL;

static face_detect_results_t g_latest_results = {0};
static std::atomic<bool> g_continuous_running{false};
static std::atomic<bool> g_is_inited{false};

static uint8_t *g_rgb888_buf = nullptr;
static uint16_t *g_shared_rgb565_buf = nullptr;
static bool g_new_frame_ready = false;
static bool g_frame_updated = false;

static int g_src_w = 0;
static int g_src_h = 0;

static HumanFaceDetectMSR01 *g_msr01_detector = nullptr;

// 相机抓取广播回调（运行在 cam_capture 任务上下文，必须快速，仅做拷贝标记）
static void on_camera_frame_cb(const camera_fb_t *fb, void *user_arg) {
  if (!g_continuous_running.load(std::memory_order_relaxed))
    return;
  if (!fb || !g_shared_rgb565_buf)
    return;

  if (xSemaphoreTake(g_data_mutex, 0) == pdTRUE) {
    if (!g_new_frame_ready) {
      if (fb->format == PIXFORMAT_RGB565) {
        memcpy(g_shared_rgb565_buf, fb->buf, fb->len);
      } else if (fb->format == PIXFORMAT_JPEG) {
        jpg2rgb565(fb->buf, fb->len, (uint8_t *)g_shared_rgb565_buf,
                   JPG_SCALE_NONE);
      }
      g_new_frame_ready = true;
    }
    xSemaphoreGive(g_data_mutex);
  }
}

static bool run_msr01_inference(uint8_t *rgb_ptr, int width, int height,
                                face_detect_results_t *out_res) {
  if (!g_msr01_detector || !rgb_ptr || !out_res)
    return false;

  std::list<dl::detect::result_t> &results =
      g_msr01_detector->infer(rgb_ptr, {height, width, 3});

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

static void draw_results_on_rgb565(uint16_t *buf, int w, int h,
                                   const face_detect_results_t &r) {
  const uint16_t COLOR_BOX = 0xF800;
  const uint16_t COLOR_POINT = 0x07E0;

  for (int i = 0; i < r.count; i++) {
    const face_info_t &f = r.faces[i];
    int x1 = DL_MAX((int)f.box[0], 0);
    int y1 = DL_MAX((int)f.box[1], 0);
    int x2 = DL_MAX((int)f.box[2], 0);
    int y2 = DL_MAX((int)f.box[3], 0);

    dl::image::draw_hollow_rectangle(buf, h, w, x1, y1, x2, y2, COLOR_BOX);

    for (int k = 0; k < f.keypoint_count; k += 2) {
      int px = DL_MAX((int)f.keypoint[k], 0);
      int py = DL_MAX((int)f.keypoint[k + 1], 0);
      dl::image::draw_point(buf, h, w, px, py, 10, COLOR_POINT);
    }
  }
}

// 慢速 AI 推理 Worker 线程
static void detector_worker_task(void *arg) {
  ESP_LOGI(TAG, "Subscribed Face Detection Worker Started");

  while (1) {
    if (g_continuous_running.load(std::memory_order_relaxed)) {
      bool has_frame = false;

      if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (g_new_frame_ready) {
          // 将 RGB565 转化为 RGB888 供模型推理
          uint8_t *src_ptr = (uint8_t *)g_shared_rgb565_buf;
          for (int i = 0; i < g_src_w * g_src_h; i++) {
            uint16_t rgb565 = g_shared_rgb565_buf[i];
            g_rgb888_buf[i * 3 + 0] = (rgb565 >> 11) << 3;
            g_rgb888_buf[i * 3 + 1] = ((rgb565 >> 5) & 0x3F) << 2;
            g_rgb888_buf[i * 3 + 2] = (rgb565 & 0x1F) << 3;
          }
          g_new_frame_ready = false;
          has_frame = true;
        }
        xSemaphoreGive(g_data_mutex);
      }

      if (has_frame) {
        // 运行 AI 推理
        face_detect_results_t temp_res = {0};
        run_msr01_inference(g_rgb888_buf, g_src_w, g_src_h, &temp_res);

        if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          g_latest_results = temp_res;
          // 直接在共享缓存上叠加框
          draw_results_on_rgb565(g_shared_rgb565_buf, g_src_w, g_src_h,
                                 g_latest_results);
          g_frame_updated = true;
          xSemaphoreGive(g_data_mutex);
        }
      }

      // 【降频关键】每次推理完成后强制 Delay，把 CPU 让给 Wi-Fi / Uploader 任务
      vTaskDelay(pdMS_TO_TICKS(FACE_DETECT_INTERVAL_MS));
    } else {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
  }
}

extern "C" bool face_detector_helper_init(int fb_width, int fb_height) {
  if (g_is_inited.load())
    return true;

  g_src_w = fb_width;
  g_src_h = fb_height;

  size_t rgb888_size = fb_width * fb_height * 3;
  g_rgb888_buf = (uint8_t *)heap_caps_malloc(rgb888_size, MALLOC_CAP_SPIRAM |
                                                              MALLOC_CAP_8BIT);

  size_t rgb565_size = fb_width * fb_height * 2;
  g_shared_rgb565_buf = (uint16_t *)heap_caps_malloc(
      rgb565_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (!g_rgb888_buf || !g_shared_rgb565_buf) {
    ESP_LOGE(TAG, "Failed to allocate memory buffers");
    return false;
  }

  g_msr01_detector = new HumanFaceDetectMSR01(0.3F, 0.3F, 10, 0.3F);
  g_data_mutex = xSemaphoreCreateMutex();

  g_continuous_running.store(false);
  xTaskCreatePinnedToCore(detector_worker_task, "fd_worker", 5 * 1024, NULL, 3,
                          &g_detector_task, 1);

  g_is_inited.store(true);
  ESP_LOGI(TAG, "Face detector helper inited");
  return true;
}

extern "C" void face_detector_helper_start_continuous(void) {
  if (!g_is_inited.load())
    return;

  bool expected = false;
  if (g_continuous_running.compare_exchange_strong(expected, true)) {
    // 订阅 Camera 硬件推流
    g_cam_sub_handle = cam_helper_subscribe(on_camera_frame_cb, NULL);
    if (!g_cam_sub_handle) {
      ESP_LOGE(TAG, "Failed to subscribe camera");
      g_continuous_running.store(false);
      return;
    }

    if (g_detector_task) {
      xTaskNotifyGive(g_detector_task);
    }
    ESP_LOGI(TAG, "Face detection started with camera subscription");
  }
}

extern "C" void face_detector_helper_stop_continuous(void) {
  if (!g_is_inited.load())
    return;

  bool expected = true;
  if (g_continuous_running.compare_exchange_strong(expected, false)) {
    // 取消订阅，如果这是唯一的订阅者，Camera 将自动进入 Standby 下电休眠
    if (g_cam_sub_handle) {
      cam_helper_unsubscribe(g_cam_sub_handle);
      g_cam_sub_handle = NULL;
    }
    ESP_LOGI(TAG, "Face detection stopped & unsubscribed");
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

  if (g_rgb888_buf)
    heap_caps_free(g_rgb888_buf);
  if (g_shared_rgb565_buf)
    heap_caps_free(g_shared_rgb565_buf);
  if (g_msr01_detector)
    delete g_msr01_detector;

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
  if (!dst_buf || !g_shared_rgb565_buf || width != g_src_w || height != g_src_h)
    return false;

  bool ret = false;
  if (g_data_mutex &&
      xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(30)) == pdTRUE) {
    if (g_frame_updated) {
      memcpy(dst_buf, g_shared_rgb565_buf,
             g_src_w * g_src_h * sizeof(uint16_t));
      g_frame_updated = false;
      ret = true;
    }
    xSemaphoreGive(g_data_mutex);
  }
  return ret;
}