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

static const char *TAG = "face_detector";

// 检测任务句柄
static TaskHandle_t g_detector_task = NULL;

// 触发信号量：外部调用触发一次检测
static SemaphoreHandle_t g_trigger_sem = NULL;
// 完成信号量：检测完成后释放，用于同步等待
static SemaphoreHandle_t g_done_sem = NULL;

// 检测结果
static int g_last_face_count = 0;
static bool g_detection_success = false;

// 时间戳记录（用于“最近有人脸”查询）
static int64_t g_last_face_detected_us = 0;

// 标志：任务是否正在处理中（防止重复触发）
static volatile bool g_is_processing = false;

// 推理核心任务（单次执行模式）
static void detector_task(void *arg) {
  // 创建检测器实例（只创建一次）
  HumanFaceDetectMSR01 detector(0.3F, 0.3F, 10, 0.3F);

  while (1) {
    // 等待触发信号
    if (xSemaphoreTake(g_trigger_sem, portMAX_DELAY) == pdTRUE) {
      g_is_processing = true;
      g_detection_success = false;
      g_last_face_count = 0;

      // 获取一帧相机图像
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb && fb->format == PIXFORMAT_YUV422) {
        size_t rgb_size = fb->width * fb->height * 3;
        uint8_t *rgb_buf =
            (uint8_t *)heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM);
        if (rgb_buf) {
          // YUV422 -> RGB888 转换
          fmt2rgb888(fb->buf, fb->len, fb->format, rgb_buf);

          // 执行推理
          auto &results =
              detector.infer(rgb_buf, {(int)fb->height, (int)fb->width, 3});
          g_last_face_count = results.size();
          g_detection_success = true;

          if (g_last_face_count > 0) {
            g_last_face_detected_us = esp_timer_get_time();
            ESP_LOGI(TAG, "Detected %d face(s)", g_last_face_count);
          } else {
            ESP_LOGI(TAG, "No face detected");
          }

          heap_caps_free(rgb_buf);
        } else {
          ESP_LOGE(TAG, "PSRAM allocation failed");
        }
      } else {
        ESP_LOGE(TAG, "Failed to get camera frame");
      }

      if (fb)
        esp_camera_fb_return(fb);

      g_is_processing = false;

      // 通知等待者检测已完成
      xSemaphoreGive(g_done_sem);
    }
  }
}

extern "C" bool face_detector_helper_init(void) {
  if (g_detector_task != NULL) {
    return true; // 已经初始化
  }

  g_trigger_sem = xSemaphoreCreateBinary();
  g_done_sem = xSemaphoreCreateBinary();
  if (g_trigger_sem == NULL || g_done_sem == NULL) {
    return false;
  }

  BaseType_t ret = xTaskCreatePinnedToCore(detector_task, "face_task", 4096,
                                           NULL, 2, &g_detector_task, 0);

  return (ret == pdPASS);
}

extern "C" bool face_detector_helper_trigger_detection(uint32_t timeout_ms) {
  if (g_detector_task == NULL || g_is_processing) {
    return false; // 未初始化或正忙
  }

  // 先清空可能残留的完成信号量（防止上次未取走）
  xSemaphoreTake(g_done_sem, 0);

  // 触发检测
  xSemaphoreGive(g_trigger_sem);

  // 等待检测完成
  if (xSemaphoreTake(g_done_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
    return g_detection_success ? true : false;
  } else {
    ESP_LOGW(TAG, "Detection timeout");
    return false; // 超时
  }
}

extern "C" bool face_detector_helper_is_busy(void) { return g_is_processing; }

extern "C" void face_detector_helper_update_timestamp(void) {
  g_last_face_detected_us = esp_timer_get_time();
}

extern "C" bool face_detector_helper_has_recent_face(int max_age_ms) {
  int64_t now = esp_timer_get_time();
  return (now - g_last_face_detected_us) < (max_age_ms * 1000LL);
}