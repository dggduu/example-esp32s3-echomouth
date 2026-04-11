#include "monitor_mamager.h"
#include "cJSON.h"
#include "esp_camera.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "face_detector_helper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "http_client_helper.h"
#include "img_queue.h"
#include "nvs_helper.h"
#include "task_manager.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "s3_helper.h"

#define TAG "MONITOR"
#define MONITOR_STACK_SIZE 8192

// 间隔边界（秒）
#define INTERVAL_MIN_SEC 30
#define INTERVAL_MAX_SEC 180

// 人脸等待阈值
#define FACE_WAIT_FAST_SEC 5      // ≤5秒内检测到人脸视为及时
#define FACE_WAIT_SLOW_SEC 30     // ≥30秒才检测到人脸视为迟缓
#define FACE_WAIT_TIMEOUT_SEC 300 // 最大等待时间（5分钟），超时放弃

// 间隔调整步长（秒）
#define INTERVAL_STEP_SEC 15

// 状态机
typedef enum {
  MONITOR_STATE_IDLE,
  MONITOR_STATE_SLEEP,
  MONITOR_STATE_WAIT_FACE,
  MONITOR_STATE_CAPTURING,
} monitor_state_t;

static TaskHandle_t monitor_task_handle = NULL;
static SemaphoreHandle_t s_interval_sem = NULL;   // 上传回调完成信号
static int s_new_interval_sec = INTERVAL_MAX_SEC; // 回调填充的新间隔
static bool s_upload_success = false;             // 上传是否成功（用于回调）
static char s_tick_id[32] = {0};                  // 回调填充的 tickId

static SemaphoreHandle_t s_pause_sem = NULL; // 用于挂起任务
static bool s_paused = false;
static SemaphoreHandle_t s_state_mutex = NULL;

static monitor_state_t s_state = MONITOR_STATE_IDLE;
static int64_t s_next_wake_time_us = 0;
static int s_current_interval_sec = INTERVAL_MAX_SEC;

// 人脸等待计时
static int64_t s_face_wait_start_us = 0;

// 回调函数声明
static void monitor_upload_callback(bool success, const char *tick_id_or_key,
                                    void *user_data);

// JPEG 压缩辅助函数（与之前相同）
static bool compress_fb_to_jpeg_file(camera_fb_t *fb, const char *filepath) {
  jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
  cfg.width = fb->width;
  cfg.height = fb->height;
  cfg.src_type = JPEG_PIXEL_FORMAT_YCbYCr;
  cfg.quality = 50;
  cfg.task_enable = false;

  jpeg_enc_handle_t enc;
  if (jpeg_enc_open(&cfg, &enc) != JPEG_ERR_OK) {
    return false;
  }

  size_t jpg_buf_size = 80 * 1024;
  uint8_t *jpg_buf = jpeg_calloc_align(jpg_buf_size, 16);
  if (!jpg_buf) {
    jpeg_enc_close(enc);
    return false;
  }

  int out_len = 0;
  esp_err_t ret =
      jpeg_enc_process(enc, fb->buf, fb->len, jpg_buf, jpg_buf_size, &out_len);
  jpeg_enc_close(enc);

  if (ret != JPEG_ERR_OK || out_len == 0) {
    jpeg_free_align(jpg_buf);
    return false;
  }

  FILE *f = fopen(filepath, "wb");
  if (!f) {
    jpeg_free_align(jpg_buf);
    return false;
  }
  fwrite(jpg_buf, 1, out_len, f);
  fclose(f);
  jpeg_free_align(jpg_buf);

  ESP_LOGI(TAG, "JPEG saved: %s (%d bytes)", filepath, out_len);
  return true;
}

// 轮询 VLM 结果（可阻塞）
static int poll_vlm_result(const char *tick_id) {
  char path[64];
  snprintf(path, sizeof(path), "/image/result/%s", tick_id);

  char resp[512];
  int interval = s_current_interval_sec; // 默认不变
  int retry = 0;
  const int max_retries = 30;

  while (retry < max_retries) {
    if (!http_get_json(path, resp, sizeof(resp))) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      retry++;
      continue;
    }

    cJSON *root = cJSON_Parse(resp);
    if (!root) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      retry++;
      continue;
    }

    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (status && strcmp(status->valuestring, "completed") == 0) {
      cJSON *interval_obj = cJSON_GetObjectItem(root, "interval");
      if (cJSON_IsNumber(interval_obj)) {
        interval = interval_obj->valueint;
        // 我们忽略 VLM 建议的间隔，仅用于记录，间隔调整由人脸等待时间决定
        ESP_LOGI(TAG, "VLM suggested interval: %d (ignored)", interval);
      }
      cJSON_Delete(root);
      return 0; // 返回0表示成功，实际间隔在外部调整
    } else if (status && strcmp(status->valuestring, "pending") == 0) {
      cJSON_Delete(root);
      vTaskDelay(pdMS_TO_TICKS(1000));
      retry++;
    } else {
      cJSON_Delete(root);
      ESP_LOGW(TAG, "VLM result: %s", status ? status->valuestring : "null");
      return -1; // 失败
    }
  }

  ESP_LOGW(TAG, "Polling timeout");
  return -1;
}

// 上传完成回调（在上传器任务中执行）
static void monitor_upload_callback(bool success, const char *tick_id_or_key,
                                    void *user_data) {
  s_upload_success = success;
  if (success) {
    strlcpy(s_tick_id, tick_id_or_key, sizeof(s_tick_id));
    // 轮询 VLM（我们仅需确认分析完成，不采用其返回的间隔）
    int ret = poll_vlm_result(s_tick_id);
    if (ret != 0) {
      ESP_LOGW(TAG, "VLM polling failed, but upload succeeded");
    }
  } else {
    ESP_LOGE(TAG, "Monitor upload failed");
  }

  // 间隔将在主循环中根据人脸等待时间调整，此处仅通知主循环继续
  xSemaphoreGive(s_interval_sem);
}

// 执行一次拍照并推入上传队列
static bool capture_and_enqueue(void) {
  int32_t device_id = 1;
  nvs_helper_get_i32("storage", "device_id", &device_id);
  int32_t task_id = task_manager_get_active_id();
  if (task_id == 0) {
    return false;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    ESP_LOGE(TAG, "Camera capture failed");
    return false;
  }

  char filepath[64];
  int64_t timestamp = esp_timer_get_time() / 1000;
  snprintf(filepath, sizeof(filepath), "/littlefs/monitor_%lld.jpg", timestamp);

  bool saved = compress_fb_to_jpeg_file(fb, filepath);
  esp_camera_fb_return(fb);
  if (!saved) {
    ESP_LOGE(TAG, "Failed to save JPEG");
    return false;
  }

  img_job_t job = {.path = {0},
                   .task_id = task_id,
                   .priority = IMG_PRIORITY_LOW,
                   .type = IMG_TYPE_MONITOR,
                   .on_complete = monitor_upload_callback,
                   .user_data = NULL,
                   .retry_count = 0};
  strlcpy(job.path, filepath, sizeof(job.path));

  if (!img_queue_push(&job)) {
    ESP_LOGE(TAG, "Upload queue full, drop capture");
    remove(filepath);
    return false;
  }

  ESP_LOGI(TAG, "Monitor capture queued: %s", filepath);
  return true;
}

// 根据人脸等待时长调整下次间隔
static int adjust_interval_by_face_wait(int64_t wait_sec) {
  int new_interval = s_current_interval_sec;

  if (wait_sec <= FACE_WAIT_FAST_SEC) {
    // 及时检测到人脸，缩短间隔
    new_interval -= INTERVAL_STEP_SEC;
    ESP_LOGI(TAG, "Face detected quickly (%lld sec), decreasing interval",
             wait_sec);
  } else if (wait_sec >= FACE_WAIT_SLOW_SEC) {
    // 检测迟缓，拉长间隔
    new_interval += INTERVAL_STEP_SEC;
    ESP_LOGI(TAG, "Face detected slowly (%lld sec), increasing interval",
             wait_sec);
  } else {
    // 中等速度，保持不变
    ESP_LOGI(TAG, "Face detected in medium time (%lld sec), interval unchanged",
             wait_sec);
  }

  // 限制边界
  if (new_interval < INTERVAL_MIN_SEC)
    new_interval = INTERVAL_MIN_SEC;
  if (new_interval > INTERVAL_MAX_SEC)
    new_interval = INTERVAL_MAX_SEC;

  return new_interval;
}

// 监控任务主循环
static void monitor_task_func(void *arg) {
  ESP_LOGI(TAG, "Monitor task started");

  s_state = MONITOR_STATE_IDLE;
  s_current_interval_sec = INTERVAL_MAX_SEC;
  s_next_wake_time_us =
      esp_timer_get_time() + (int64_t)s_current_interval_sec * 1000000;

  while (1) {
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    if (s_paused) {
      xSemaphoreGive(s_state_mutex);
      // 等待恢复信号
      xSemaphoreTake(s_pause_sem, portMAX_DELAY);
      // 恢复后重新获取状态
      continue;
    }
    xSemaphoreGive(s_state_mutex);

    int32_t active_task = task_manager_get_active_id();

    if (active_task == 0) {
      if (s_state != MONITOR_STATE_IDLE) {
        s_state = MONITOR_STATE_IDLE;
        ESP_LOGI(TAG, "No active task, entering IDLE");
      }
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    int64_t now_us = esp_timer_get_time();

    switch (s_state) {
    case MONITOR_STATE_IDLE:
      s_current_interval_sec = INTERVAL_MAX_SEC;
      s_next_wake_time_us = now_us + (int64_t)s_current_interval_sec * 1000000;
      s_state = MONITOR_STATE_SLEEP;
      ESP_LOGI(TAG, "Task active, initial interval %d sec",
               s_current_interval_sec);
      break;

    case MONITOR_STATE_SLEEP:
      if (now_us >= s_next_wake_time_us) {
        s_state = MONITOR_STATE_WAIT_FACE;
        s_face_wait_start_us = now_us;
        ESP_LOGI(TAG, "Interval reached, waiting for face...");
      } else {
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
      break;

    case MONITOR_STATE_WAIT_FACE:
      if (face_detector_helper_has_recent_face(1000)) {
        int64_t wait_sec =
            (esp_timer_get_time() - s_face_wait_start_us) / 1000000;
        ESP_LOGI(TAG, "Face detected after %lld sec", wait_sec);
        s_state = MONITOR_STATE_CAPTURING;
      } else {
        int64_t elapsed_sec =
            (esp_timer_get_time() - s_face_wait_start_us) / 1000000;
        if (elapsed_sec >= FACE_WAIT_TIMEOUT_SEC) {
          ESP_LOGW(TAG, "Face wait timeout, skipping this cycle");
          // 超时视为等待很长，拉长间隔
          s_current_interval_sec =
              adjust_interval_by_face_wait(FACE_WAIT_SLOW_SEC);
          s_next_wake_time_us =
              esp_timer_get_time() + (int64_t)s_current_interval_sec * 1000000;
          s_state = MONITOR_STATE_SLEEP;
        } else {
          vTaskDelay(pdMS_TO_TICKS(500));
        }
      }
      break;

    case MONITOR_STATE_CAPTURING: {
      // 执行拍照入队
      bool capture_ok = capture_and_enqueue();
      int64_t wait_sec =
          (esp_timer_get_time() - s_face_wait_start_us) / 1000000;

      if (!capture_ok) {
        // 拍照失败，视为异常，拉长间隔
        ESP_LOGE(TAG, "Capture failed, increasing interval");
        s_current_interval_sec += INTERVAL_STEP_SEC;
        if (s_current_interval_sec > INTERVAL_MAX_SEC)
          s_current_interval_sec = INTERVAL_MAX_SEC;
      } else {
        // 等待上传回调完成（最多等待 60 秒）
        if (xSemaphoreTake(s_interval_sem, pdMS_TO_TICKS(60000)) == pdTRUE) {
          if (s_upload_success) {
            // 上传成功，根据人脸等待时间调整间隔
            s_current_interval_sec = adjust_interval_by_face_wait(wait_sec);
          } else {
            // 上传失败，拉长间隔
            ESP_LOGW(TAG, "Upload failed, increasing interval");
            s_current_interval_sec += INTERVAL_STEP_SEC;
            if (s_current_interval_sec > INTERVAL_MAX_SEC)
              s_current_interval_sec = INTERVAL_MAX_SEC;
          }
        } else {
          // 回调超时，拉长间隔
          ESP_LOGW(TAG, "Upload callback timeout, increasing interval");
          s_current_interval_sec += INTERVAL_STEP_SEC;
          if (s_current_interval_sec > INTERVAL_MAX_SEC)
            s_current_interval_sec = INTERVAL_MAX_SEC;
        }
      }

      // 设置下次唤醒时间并回到睡眠
      s_next_wake_time_us =
          esp_timer_get_time() + (int64_t)s_current_interval_sec * 1000000;
      s_state = MONITOR_STATE_SLEEP;
      ESP_LOGI(TAG, "Next wake in %d sec", s_current_interval_sec);
      break;
    }
    }
  }
}

void monitor_task_start(void) {
  if (s_interval_sem == NULL) {
    s_interval_sem = xSemaphoreCreateBinary();
  }
  if (s_pause_sem == NULL) {
    s_pause_sem = xSemaphoreCreateBinary();
  }

  if (s_state_mutex == NULL) {
    s_state_mutex = xSemaphoreCreateBinary();
  }
  xTaskCreate(monitor_task_func, "monitor", MONITOR_STACK_SIZE, NULL, 5,
              &monitor_task_handle);
}

void monitor_task_reset_timer(void) {
  s_state = MONITOR_STATE_WAIT_FACE;
  s_face_wait_start_us = esp_timer_get_time();
  s_current_interval_sec = INTERVAL_MAX_SEC;
  ESP_LOGI(TAG, "Timer reset, will capture immediately upon face detection");
}

void monitor_task_pause(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  if (!s_paused) {
    s_paused = true;
    ESP_LOGI(TAG, "Monitor task paused");
  }
  xSemaphoreGive(s_state_mutex);
}

void monitor_task_resume(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  if (s_paused) {
    s_paused = false;
    ESP_LOGI(TAG, "Monitor task resumed");
    xSemaphoreGive(s_pause_sem); // 唤醒挂起的任务
  }
  xSemaphoreGive(s_state_mutex);
}