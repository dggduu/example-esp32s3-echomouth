#include "face_detector_helper.h"
#include "dl_image.hpp"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "human_face_detect_msr01.hpp"
#include <atomic>

static const char *TAG = "face_detector";

// 检测任务句柄
static TaskHandle_t g_detector_task = NULL;

// 触发信号量：外部调用触发一次检测
static SemaphoreHandle_t g_trigger_sem = NULL;
// 完成信号量：检测完成后释放，用于同步等待
static SemaphoreHandle_t g_done_sem = NULL;

// 检测结果（受互斥锁保护）
static int g_last_face_count = 0;
static bool g_detection_success = false;
static int64_t g_last_face_detected_us = 0; // 上次检测到人脸的时间戳

// 标志：任务是否正在处理中（原子操作）
static std::atomic<bool> g_is_processing{false};

// 互斥锁保护共享数据（时间戳和计数）
static SemaphoreHandle_t g_data_mutex = NULL;

// 预分配的 RGB 缓冲区（复用）
static uint8_t *g_rgb_buf = nullptr;
static size_t g_rgb_buf_size = 0;
static int g_fb_width = 0;
static int g_fb_height = 0;

// 推理核心任务（单次执行模式）
static void detector_task(void *arg) {
  // 创建检测器实例（只创建一次）
  HumanFaceDetectMSR01 detector(0.3F, 0.3F, 10, 0.3F);

  while (1) {
    // 等待触发信号
    if (xSemaphoreTake(g_trigger_sem, portMAX_DELAY) == pdTRUE) {
      g_is_processing.store(true, std::memory_order_release);

      // 临时结果变量
      int face_count = 0;
      bool detection_ok = false;
      int64_t now_us = 0;

      // 获取一帧相机图像
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb && fb->format == PIXFORMAT_YUV422) {
        // 检查缓冲区是否足够
        size_t required_size = fb->width * fb->height * 3;
        if (g_rgb_buf && g_rgb_buf_size >= required_size &&
            fb->width == g_fb_width && fb->height == g_fb_height) {

          // YUV422 -> RGB888 转换
          fmt2rgb888(fb->buf, fb->len, fb->format, g_rgb_buf);

          // 执行推理
          auto &results =
              detector.infer(g_rgb_buf, {(int)fb->height, (int)fb->width, 3});
          face_count = results.size();
          detection_ok = true;
          now_us = esp_timer_get_time();

          if (face_count > 0) {
            ESP_LOGI(TAG, "Detected %d face(s)", face_count);
          } else {
            ESP_LOGI(TAG, "No face detected");
          }
        } else {
          ESP_LOGE(TAG, "Buffer size mismatch or not allocated");
        }
      } else {
        ESP_LOGE(TAG, "Failed to get camera frame or wrong format");
      }

      if (fb)
        esp_camera_fb_return(fb);

      // 更新共享数据（加锁）
      if (g_data_mutex) {
        xSemaphoreTake(g_data_mutex, portMAX_DELAY);
        g_last_face_count = face_count;
        g_detection_success = detection_ok;
        if (detection_ok && face_count > 0) {
          g_last_face_detected_us = now_us;
        }
        xSemaphoreGive(g_data_mutex);
      }

      g_is_processing.store(false, std::memory_order_release);

      // 通知等待者检测已完成
      xSemaphoreGive(g_done_sem);
    }
  }
}

extern "C" bool face_detector_helper_init(int fb_width, int fb_height) {
  if (g_detector_task != NULL) {
    return true; // 已经初始化
  }

  // 保存分辨率，用于缓冲区分配
  g_fb_width = fb_width;
  g_fb_height = fb_height;

  // 分配 RGB 缓冲区（PSRAM 优先）
  size_t buf_size = fb_width * fb_height * 3;
  g_rgb_buf = (uint8_t *)heap_caps_malloc(buf_size,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!g_rgb_buf) {
    // 尝试内部 RAM
    g_rgb_buf = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL |
                                                          MALLOC_CAP_8BIT);
    if (!g_rgb_buf) {
      ESP_LOGE(TAG, "Failed to allocate RGB buffer (%zu bytes)", buf_size);
      return false;
    }
  }
  g_rgb_buf_size = buf_size;
  ESP_LOGI(TAG, "Allocated RGB buffer %zu bytes", buf_size);

  g_trigger_sem = xSemaphoreCreateBinary();
  g_done_sem = xSemaphoreCreateBinary();
  g_data_mutex = xSemaphoreCreateMutex();
  if (!g_trigger_sem || !g_done_sem || !g_data_mutex) {
    ESP_LOGE(TAG, "Failed to create semaphores");
    if (g_rgb_buf)
      heap_caps_free(g_rgb_buf);
    return false;
  }

  BaseType_t ret = xTaskCreatePinnedToCore(detector_task, "face_task", 4096,
                                           NULL, 2, &g_detector_task, 0);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create task");
    vSemaphoreDelete(g_trigger_sem);
    vSemaphoreDelete(g_done_sem);
    vSemaphoreDelete(g_data_mutex);
    heap_caps_free(g_rgb_buf);
    return false;
  }

  return true;
}

extern "C" void face_detector_helper_deinit(void) {
  if (g_detector_task) {
    vTaskDelete(g_detector_task);
    g_detector_task = NULL;
  }
  if (g_trigger_sem) {
    vSemaphoreDelete(g_trigger_sem);
    g_trigger_sem = NULL;
  }
  if (g_done_sem) {
    vSemaphoreDelete(g_done_sem);
    g_done_sem = NULL;
  }
  if (g_data_mutex) {
    vSemaphoreDelete(g_data_mutex);
    g_data_mutex = NULL;
  }
  if (g_rgb_buf) {
    heap_caps_free(g_rgb_buf);
    g_rgb_buf = nullptr;
    g_rgb_buf_size = 0;
  }
  g_is_processing.store(false, std::memory_order_relaxed);
  ESP_LOGI(TAG, "Deinitialized");
}

extern "C" bool face_detector_helper_trigger_detection(uint32_t timeout_ms) {
  if (g_detector_task == NULL)
    return false;

  // 如果正在处理中，直接返回 false（调用方可选择稍后重试）
  if (g_is_processing.load(std::memory_order_acquire)) {
    return false;
  }

  // 先清空可能残留的完成信号量（防止上次未取走）
  xSemaphoreTake(g_done_sem, 0);

  // 触发检测
  xSemaphoreGive(g_trigger_sem);

  // 等待检测完成
  if (xSemaphoreTake(g_done_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
    bool success = false;
    if (g_data_mutex) {
      xSemaphoreTake(g_data_mutex, portMAX_DELAY);
      success = g_detection_success && (g_last_face_count > 0);
      xSemaphoreGive(g_data_mutex);
    }
    return success;
  } else {
    ESP_LOGW(TAG, "Detection timeout");
    return false;
  }
}

extern "C" bool face_detector_helper_is_busy(void) {
  return g_is_processing.load(std::memory_order_acquire);
}

extern "C" void face_detector_helper_update_timestamp(void) {
  if (g_data_mutex) {
    xSemaphoreTake(g_data_mutex, portMAX_DELAY);
    g_last_face_detected_us = esp_timer_get_time();
    xSemaphoreGive(g_data_mutex);
  }
}

extern "C" bool face_detector_helper_has_recent_face(int max_age_ms) {
  if (!g_data_mutex)
    return false;

  int64_t now = esp_timer_get_time();
  bool recent = false;

  xSemaphoreTake(g_data_mutex, portMAX_DELAY);
  recent = (now - g_last_face_detected_us) < (max_age_ms * 1000LL);
  xSemaphoreGive(g_data_mutex);

  return recent;
}