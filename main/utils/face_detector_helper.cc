#include "face_detector_helper.h"
#include "dl_image.hpp"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "human_face_detect_msr01.hpp"

static const char *TAG = "ai_bridge";
static SemaphoreHandle_t xTriggerAI = NULL;
static bool is_processing = false;

// 封装推理核心逻辑
static void task_process_ai_single_shot(void *arg) {
  auto *detector = new HumanFaceDetectMSR01(0.3F, 0.3F, 10, 0.3F);

  while (true) {
    if (xSemaphoreTake(xTriggerAI, portMAX_DELAY) == pdTRUE) {
      is_processing = true;
      camera_fb_t *fb = esp_camera_fb_get();

      if (fb && fb->format == PIXFORMAT_YUV422) {
        size_t rgb_size = fb->width * fb->height * 3;
        uint8_t *rgb_buf =
            (uint8_t *)heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM);

        if (rgb_buf) {
          // YUV422 -> RGB888 转换
          fmt2rgb888(fb->buf, fb->len, fb->format, rgb_buf);

          // 推理
          auto &results =
              detector->infer(rgb_buf, {(int)fb->height, (int)fb->width, 3});

          ESP_LOGI(TAG, "Detection completed. Found: %d", results.size());

          heap_caps_free(rgb_buf);
        } else {
          ESP_LOGE(TAG, "PSRAM allocation failed");
        }
      }

      if (fb)
        esp_camera_fb_return(fb);
      is_processing = false;
    }
  }
}

extern "C" bool face_detector_helper_init(void) {
  if (xTriggerAI != NULL)
    return true; // 防止重复初始化

  xTriggerAI = xSemaphoreCreateBinary();
  if (xTriggerAI == NULL)
    return false;

  BaseType_t ret = xTaskCreatePinnedToCore(task_process_ai_single_shot,
                                           "ai_task", 4096, NULL, 2, NULL, 0);

  return (ret == pdPASS);
}

extern "C" bool face_detector_helper_trigger_detection(void) {
  if (xTriggerAI == NULL || is_processing) {
    return false;
  }
  return (xSemaphoreGive(xTriggerAI) == pdTRUE);
}

extern "C" bool face_detector_helper_is_busy(void) { return is_processing; }

static int64_t last_face_detected_us = 0;

extern "C" void face_detector_helper_update_timestamp(void) {
  last_face_detected_us = esp_timer_get_time();
}

extern "C" bool face_detector_helper_has_recent_face(int max_age_ms) {
  int64_t now = esp_timer_get_time();
  return (now - last_face_detected_us) < (max_age_ms * 1000);
}