#include "monitor_mamager.h"
#include "cJSON.h"
#include "cam_helper.h"
#include "esp_camera.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "face_detector_helper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "http_client_helper.h"
#include "img_queue.h"
#include "nvs_helper.h"
#include "task_manager.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define TAG "MONITOR"

#include "time_test_helper.h"

#define MONITOR_STACK_SIZE 4096
#define VLM_POLL_STACK_SIZE 4096

#define INTERVAL_MIN_SEC (60 * 1)
#define INTERVAL_MAX_SEC (60 * 30)

#define FACE_WAIT_FAST_SEC 20
#define FACE_WAIT_SLOW_SEC 60

#define FACE_WAIT_TIMEOUT_SEC 30
#define INTERVAL_STEP_SEC 15
#define FACE_POLL_INTERVAL_MS 15000
#define UPLOAD_CALLBACK_TIMEOUT_MS 30000

#define VLM_POLL_INTERVAL_MS 15000
#define VLM_MAX_RETRIES 8

typedef struct {
  char tick_id[32];
  bool success;
  int suggested_interval;
} vlm_result_msg_t;

static QueueHandle_t s_vlm_result_queue = NULL;

typedef enum {
  MONITOR_STATE_IDLE,
  MONITOR_STATE_SLEEP,
  MONITOR_STATE_WAIT_FACE,
  MONITOR_STATE_CAPTURING,
} monitor_state_t;

static TaskHandle_t s_monitor_task_handle = NULL;
static TaskHandle_t s_vlm_poll_task_handle = NULL;

static StaticTask_t s_vlm_task_tcb;
static StackType_t *s_vlm_task_stack = NULL;

static SemaphoreHandle_t s_upload_done_sem = NULL;
static SemaphoreHandle_t s_monitor_mutex = NULL;
static monitor_state_t s_state = MONITOR_STATE_IDLE;
static int64_t s_next_wake_time_us = 0;
static int s_current_interval_sec = INTERVAL_MIN_SEC;
static int64_t s_face_wait_start_us = 0;

static void monitor_task_func(void *arg);
static void vlm_poll_task_func(void *arg);
static void monitor_upload_callback(bool success, const char *tick_id_or_key,
                                    void *user_data);
static int adjust_interval_by_face_wait(int64_t wait_sec);
static bool capture_and_enqueue(void);

static int adjust_interval_by_face_wait(int64_t wait_sec) {
  int new_interval = s_current_interval_sec;
  if (wait_sec <= FACE_WAIT_FAST_SEC) {
    new_interval -= INTERVAL_STEP_SEC;
    ESP_LOGI(TAG, "Fast face detection (%lld s) → interval -%d", wait_sec,
             INTERVAL_STEP_SEC);
  } else if (wait_sec >= FACE_WAIT_SLOW_SEC) {
    new_interval += INTERVAL_STEP_SEC;
    ESP_LOGI(TAG, "Slow face detection (%lld s) → interval +%d", wait_sec,
             INTERVAL_STEP_SEC);
  }
  if (new_interval < INTERVAL_MIN_SEC)
    new_interval = INTERVAL_MIN_SEC;
  if (new_interval > INTERVAL_MAX_SEC)
    new_interval = INTERVAL_MAX_SEC;
  return new_interval;
}

static bool capture_and_enqueue(void) {
  int32_t device_id = 1;
  nvs_helper_get_i32("storage", "device_id", &device_id);
  int32_t task_id = task_manager_get_active_id();
  if (task_id == 0) {
    ESP_LOGW(TAG, "No active task, skip capture");
    return false;
  }

  camera_fb_t *fb = cam_helper_get_fb();
  if (!fb) {
    ESP_LOGE(TAG, "Camera capture failed");
    return false;
  }

  char filepath[64];
  int64_t timestamp = esp_timer_get_time() / 1000;
  snprintf(filepath, sizeof(filepath), "/littlefs/monitor_%lld.jpg", timestamp);

  jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
  cfg.width = fb->width;
  cfg.height = fb->height;
  cfg.src_type = JPEG_PIXEL_FORMAT_YCbYCr;
  cfg.quality = 50;
  cfg.task_enable = false;

  jpeg_enc_handle_t enc;
  if (jpeg_enc_open(&cfg, &enc) != JPEG_ERR_OK) {
    cam_helper_return_fb(fb);
    return false;
  }

  size_t jpg_buf_size = 80 * 1024;
  uint8_t *jpg_buf = jpeg_calloc_align(jpg_buf_size, 16);
  if (!jpg_buf) {
    jpeg_enc_close(enc);
    cam_helper_return_fb(fb);
    return false;
  }

  int out_len = 0;
  esp_err_t ret =
      jpeg_enc_process(enc, fb->buf, fb->len, jpg_buf, jpg_buf_size, &out_len);
  jpeg_enc_close(enc);
  cam_helper_return_fb(fb);

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

  img_job_t job = {
      .task_id = task_id,
      .priority = IMG_PRIORITY_LOW,
      .type = IMG_TYPE_MONITOR,
      .on_complete = monitor_upload_callback,
      .user_data = NULL,
      .retry_count = 0,
  };
  strlcpy(job.path, filepath, sizeof(job.path));

  if (!img_queue_push(&job)) {
    ESP_LOGE(TAG, "Upload queue full, drop capture");
    remove(filepath);
    return false;
  }
  return true;
}

static void monitor_upload_callback(bool success, const char *tick_id_or_key,
                                    void *user_data) {
  ESP_LOGI(TAG, "Upload callback: success=%d, tick=%s", success,
           tick_id_or_key ? tick_id_or_key : "NULL");
  if (!success) {
    xSemaphoreGive(s_upload_done_sem);
    return;
  }
  if (s_vlm_result_queue) {
    vlm_result_msg_t msg = {0};
    msg.success = true;
    strlcpy(msg.tick_id, tick_id_or_key, sizeof(msg.tick_id));
    if (xQueueSend(s_vlm_result_queue, &msg, 0) != pdTRUE) {
      ESP_LOGW(TAG, "VLM queue full");
      xSemaphoreGive(s_upload_done_sem);
    }
  } else {
    xSemaphoreGive(s_upload_done_sem);
  }
}

static void vlm_poll_task_func(void *arg) {
  vlm_result_msg_t msg;
  ESP_LOGI(TAG, "VLM poll task started");
  while (1) {
    if (xQueueReceive(s_vlm_result_queue, &msg, portMAX_DELAY) != pdTRUE)
      continue;
    if (!msg.success)
      continue;

    char path[64];
    snprintf(path, sizeof(path), "/device/image/result/%s", msg.tick_id);
    char resp[512];
    bool vlm_ok = false;
    for (int retry = 0; retry < VLM_MAX_RETRIES; retry++) {
      if (!http_get_json(path, resp, sizeof(resp))) {
        vTaskDelay(pdMS_TO_TICKS(VLM_POLL_INTERVAL_MS));
        continue;
      }
      cJSON *root = cJSON_Parse(resp);
      if (!root) {
        vTaskDelay(pdMS_TO_TICKS(VLM_POLL_INTERVAL_MS));
        continue;
      }
      cJSON *status = cJSON_GetObjectItem(root, "status");
      if (status && strcmp(status->valuestring, "completed") == 0) {
        vlm_ok = true;
        cJSON_Delete(root);
        break;
      } else if (status && strcmp(status->valuestring, "pending") == 0) {
        cJSON_Delete(root);
        vTaskDelay(pdMS_TO_TICKS(VLM_POLL_INTERVAL_MS));
        continue;
      } else {
        cJSON_Delete(root);
        break;
      }
    }
    if (vlm_ok) {
      ESP_LOGI(TAG, "VLM completed for tick %s", msg.tick_id);
    } else {
      ESP_LOGW(TAG, "VLM failed for tick %s", msg.tick_id);
    }
    xSemaphoreGive(s_upload_done_sem);
  }
}

static void monitor_task_func(void *arg) {
  ESP_LOGI(TAG, "Monitor task started (internal SRAM stack)");
  s_state = MONITOR_STATE_IDLE;
  s_current_interval_sec = INTERVAL_MIN_SEC;
  s_next_wake_time_us =
      esp_timer_get_time() + (int64_t)s_current_interval_sec * 1000000;

  while (1) {
    int32_t active_task = task_manager_get_active_id();
    if (active_task == 0) {
      if (s_state != MONITOR_STATE_IDLE) {
        ESP_LOGI(TAG, "No active task, enter IDLE");
        s_state = MONITOR_STATE_IDLE;
        s_current_interval_sec = INTERVAL_MIN_SEC;
      }
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    int64_t now_us = esp_timer_get_time();
    switch (s_state) {
    case MONITOR_STATE_IDLE:
      s_current_interval_sec = INTERVAL_MIN_SEC;
      s_next_wake_time_us = now_us + (int64_t)s_current_interval_sec * 1000000;
      s_state = MONITOR_STATE_SLEEP;
      ESP_LOGI(TAG, "IDLE->SLEEP, interval=%d sec", s_current_interval_sec);
      break;
    case MONITOR_STATE_SLEEP:
      if (now_us >= s_next_wake_time_us) {
        ESP_LOGI(TAG, "Interval reached, WAIT_FACE");
        cam_helper_acquire();
        s_state = MONITOR_STATE_WAIT_FACE;
        s_face_wait_start_us = now_us;
      } else {
        int64_t remain_ms = (s_next_wake_time_us - now_us) / 1000;
        vTaskDelay(pdMS_TO_TICKS((remain_ms > 1000) ? 1000 : remain_ms));
      }
      break;
    case MONITOR_STATE_WAIT_FACE:
      if (face_detector_helper_trigger_detection(100)) {
        int64_t wait_sec =
            (esp_timer_get_time() - s_face_wait_start_us) / 1000000;
        ESP_LOGI(TAG, "Face detected after %lld sec -> CAPTURING", wait_sec);
        s_state = MONITOR_STATE_CAPTURING;
      } else {
        int64_t elapsed_sec =
            (esp_timer_get_time() - s_face_wait_start_us) / 1000000;
        if (elapsed_sec >= FACE_WAIT_TIMEOUT_SEC) {
          ESP_LOGW(TAG, "Face wait timeout, skip cycle");
          s_current_interval_sec =
              adjust_interval_by_face_wait(FACE_WAIT_SLOW_SEC);
          s_next_wake_time_us =
              esp_timer_get_time() + (int64_t)s_current_interval_sec * 1000000;
          s_state = MONITOR_STATE_SLEEP;
        } else {
          vTaskDelay(pdMS_TO_TICKS(FACE_POLL_INTERVAL_MS));
        }
      }
      break;
    case MONITOR_STATE_CAPTURING: {
      int64_t face_wait_sec =
          (esp_timer_get_time() - s_face_wait_start_us) / 1000000;
      ESP_LOGI(TAG, "Capturing, face wait=%lld sec", face_wait_sec);
      bool ok = capture_and_enqueue();
      if (!ok) {
        ESP_LOGE(TAG, "Capture failed");
        s_current_interval_sec += INTERVAL_STEP_SEC;
        if (s_current_interval_sec > INTERVAL_MAX_SEC)
          s_current_interval_sec = INTERVAL_MAX_SEC;
        s_next_wake_time_us =
            esp_timer_get_time() + (int64_t)s_current_interval_sec * 1000000;
        cam_helper_release();
        s_state = MONITOR_STATE_SLEEP;
        break;
      }
      if (xSemaphoreTake(s_upload_done_sem,
                         pdMS_TO_TICKS(UPLOAD_CALLBACK_TIMEOUT_MS)) == pdTRUE) {
        s_current_interval_sec = adjust_interval_by_face_wait(face_wait_sec);
      } else {
        ESP_LOGW(TAG, "Upload/VLM timeout");
        s_current_interval_sec += INTERVAL_STEP_SEC;
        if (s_current_interval_sec > INTERVAL_MAX_SEC)
          s_current_interval_sec = INTERVAL_MAX_SEC;
      }
      s_next_wake_time_us =
          esp_timer_get_time() + (int64_t)s_current_interval_sec * 1000000;
      cam_helper_release();
      s_state = MONITOR_STATE_SLEEP;
      ESP_LOGI(TAG, "Cycle done, next wake in %d sec", s_current_interval_sec);
      break;
    }
    default:
      s_state = MONITOR_STATE_IDLE;
      break;
    }
  }
}

void monitor_task_start(void) {
  if (s_upload_done_sem == NULL) {
    s_upload_done_sem = xSemaphoreCreateBinary();
    if (!s_upload_done_sem) {
      ESP_LOGE(TAG, "Semaphore failed");
      return;
    }
  }
  if (s_monitor_mutex == NULL) {
    s_monitor_mutex = xSemaphoreCreateMutex();
    if (!s_monitor_mutex) {
      ESP_LOGE(TAG, "Mutex failed");
      return;
    }
  }
  if (s_vlm_result_queue == NULL) {
    s_vlm_result_queue = xQueueCreate(5, sizeof(vlm_result_msg_t));
    if (!s_vlm_result_queue) {
      ESP_LOGE(TAG, "Queue failed");
      return;
    }
  }

  if (s_vlm_poll_task_handle == NULL) {
    size_t stack_words = VLM_POLL_STACK_SIZE / sizeof(StackType_t);
    s_vlm_task_stack = (StackType_t *)heap_caps_malloc(
        VLM_POLL_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_vlm_task_stack) {
      s_vlm_poll_task_handle =
          xTaskCreateStatic(vlm_poll_task_func, "vlm_poll", stack_words, NULL,
                            3, s_vlm_task_stack, &s_vlm_task_tcb);
      if (s_vlm_poll_task_handle) {
        ESP_LOGI(TAG, "VLM poll task created (PSRAM stack, %d bytes)",
                 VLM_POLL_STACK_SIZE);
      } else {
        heap_caps_free(s_vlm_task_stack);
        s_vlm_task_stack = NULL;
        xTaskCreate(vlm_poll_task_func, "vlm_poll", VLM_POLL_STACK_SIZE, NULL,
                    3, &s_vlm_poll_task_handle);
      }
    } else {
      xTaskCreate(vlm_poll_task_func, "vlm_poll", VLM_POLL_STACK_SIZE, NULL, 3,
                  &s_vlm_poll_task_handle);
    }
  }

  if (s_monitor_task_handle == NULL) {
    BaseType_t ret =
        xTaskCreate(monitor_task_func, "monitor", MONITOR_STACK_SIZE, NULL, 5,
                    &s_monitor_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Monitor task creation failed");
      return;
    }
    ESP_LOGI(TAG, "Monitor task created (internal SRAM, %d bytes)",
             MONITOR_STACK_SIZE);
  }

  TEST_MEM_INFO(TAG);
  ESP_LOGI(TAG, "Monitor system started");
}

void monitor_task_reset_timer(void) {
  if (s_monitor_mutex) {
    xSemaphoreTake(s_monitor_mutex, portMAX_DELAY);
    s_state = MONITOR_STATE_SLEEP;
    s_next_wake_time_us = esp_timer_get_time() + 1000000LL;
    s_current_interval_sec = INTERVAL_MIN_SEC;
    xSemaphoreGive(s_monitor_mutex);
    ESP_LOGI(TAG, "Timer reset with 1s delay before next wake");
  }
}

void monitor_task_pause(void) {
  if (s_monitor_task_handle)
    ESP_LOGI(TAG, "moitor task pause");
  vTaskSuspend(s_monitor_task_handle);
}

void monitor_task_resume(void) {
  if (s_monitor_task_handle) {
    ESP_LOGI(TAG, "moitor task resume");
    monitor_task_reset_timer();
    vTaskResume(s_monitor_task_handle);
  }
}