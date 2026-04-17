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

static TaskHandle_t g_detector_task = NULL;

static SemaphoreHandle_t g_trigger_sem = NULL;

static SemaphoreHandle_t g_done_sem = NULL;

static int g_last_face_count = 0;
static bool g_detection_success = false;
static int64_t g_last_face_detected_us = 0;

static std::atomic<bool> g_is_processing{false};

static SemaphoreHandle_t g_data_mutex = NULL;

static uint8_t *g_rgb_buf = nullptr;
static size_t g_rgb_buf_size = 0;
static int g_fb_width = 0;
static int g_fb_height = 0;

static void detector_task(void *arg) {

  HumanFaceDetectMSR01 detector(0.3F, 0.3F, 10, 0.3F);

  while (1) {

    if (xSemaphoreTake(g_trigger_sem, portMAX_DELAY) == pdTRUE) {
      g_is_processing.store(true, std::memory_order_release);

      int face_count = 0;
      bool detection_ok = false;
      int64_t now_us = 0;

      camera_fb_t *fb = esp_camera_fb_get();
      if (fb && fb->format == PIXFORMAT_YUV422) {

        size_t required_size = fb->width * fb->height * 3;
        if (g_rgb_buf && g_rgb_buf_size >= required_size &&
            fb->width == g_fb_width && fb->height == g_fb_height) {

          fmt2rgb888(fb->buf, fb->len, fb->format, g_rgb_buf);

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

      xSemaphoreGive(g_done_sem);
    }
  }
}

extern "C" bool face_detector_helper_init(int fb_width, int fb_height) {
  if (g_detector_task != NULL) {
    return true;
  }

  g_fb_width = fb_width;
  g_fb_height = fb_height;

  size_t buf_size = fb_width * fb_height * 3;
  g_rgb_buf = (uint8_t *)heap_caps_malloc(buf_size,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!g_rgb_buf) {

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

  if (g_is_processing.load(std::memory_order_acquire)) {
    return false;
  }

  xSemaphoreTake(g_done_sem, 0);

  xSemaphoreGive(g_trigger_sem);

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