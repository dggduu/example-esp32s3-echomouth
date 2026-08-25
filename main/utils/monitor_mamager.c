#include "monitor_mamager.h"
#include "cJSON.h"
#include "cam_helper.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "face_detector_helper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "http_client_helper.h"
#include "img_converters.h" // 提供 fmt2rgb888 函数
#include "img_queue.h"
#include "nvs_helper.h"
#include "task_manager.h"
#include "time_test_helper.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define TAG "MONITOR"

#define MONITOR_STACK_SIZE 8192
#define VLM_POLL_STACK_SIZE 8192

/* 演示模式: 服务端推理返回的检测间隔为 8-15s, 本地也收敛到同一窗口,
 * 否则 apply_server_interval 会把 8-15s 又钳回 ≥60s, 动态间隔逻辑不生效 */
#define INTERVAL_MIN_SEC 8
#define INTERVAL_MAX_SEC 15

#define FACE_WAIT_FAST_SEC 8
#define FACE_WAIT_SLOW_SEC 15
#define FACE_WAIT_TIMEOUT_SEC 10
#define INTERVAL_STEP_SEC 3
#define FACE_POLL_INTERVAL_MS 2000
#define UPLOAD_CALLBACK_TIMEOUT_MS 30000
/* 相机冷启动（上电 + esp_camera_init，含失败重试）最长等待时间 */
#define CAM_POWERUP_TIMEOUT_MS 8000

#define VLM_POLL_INTERVAL_MS 15000
#define VLM_MAX_RETRIES 8

typedef struct {
  char tick_id[32];
  bool success;
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
/* monitor 任务栈（PSRAM 静态分配，见 monitor_task_start） */
static StaticTask_t s_monitor_task_tcb;
static StackType_t *s_monitor_task_stack = NULL;

static SemaphoreHandle_t s_upload_done_sem = NULL;
static SemaphoreHandle_t s_monitor_mutex = NULL;

/* 用于 cam_helper 异步推帧捕获的同步上下文 */
typedef struct {
  SemaphoreHandle_t sem;
  uint8_t *jpeg_buf;
  size_t jpeg_len;
  bool captured;
} cam_capture_ctx_t;

static monitor_state_t s_state = MONITOR_STATE_IDLE;
static int64_t s_next_wake_time_us = 0;
static int s_current_interval_sec = INTERVAL_MIN_SEC;
static int64_t s_face_wait_start_us = 0;
static volatile bool s_is_paused = false;
/* 调试页暂停期间触发强制拍照：临时解除暂停执行一轮，完成后自动恢复暂停 */
static volatile bool s_pause_after_capture = false;
/* 强制拍照请求挂起标记：monitor 可能正阻塞在上传回调等待里，
 * 仅改 s_state 会被旧循环吞掉（等待结束后无条件进 SLEEP），
 * 需要此标记让等待循环提前退出并在本轮后立即执行 */
static volatile bool s_force_capture_pending = false;

/* 服务端返回的动态检测间隔（秒），由 vlm_poll 任务写入、monitor 任务消费。
 * -1 表示暂无待应用的值；到达后 monitor 在 SLEEP 阶段应用并重置唤醒计时，
 * 服务端间隔优先于本地 adjust_interval_by_face_wait（后者仅作兜底）。 */
static int s_server_interval_sec = -1;
static SemaphoreHandle_t s_interval_mutex = NULL;

static int adjust_interval_by_face_wait(int64_t wait_sec) {
  int new_interval = s_current_interval_sec;
  if (wait_sec <= FACE_WAIT_FAST_SEC) {
    new_interval -= INTERVAL_STEP_SEC;
    ESP_LOGI(TAG, "Fast face detection (%lld s) -> interval -%d", wait_sec,
             INTERVAL_STEP_SEC);
  } else if (wait_sec >= FACE_WAIT_SLOW_SEC) {
    new_interval += INTERVAL_STEP_SEC;
    ESP_LOGI(TAG, "Slow face detection (%lld s) -> interval +%d", wait_sec,
             INTERVAL_STEP_SEC);
  }
  if (new_interval < INTERVAL_MIN_SEC)
    new_interval = INTERVAL_MIN_SEC;
  if (new_interval > INTERVAL_MAX_SEC)
    new_interval = INTERVAL_MAX_SEC;
  return new_interval;
}

/* vlm_poll 轮询到服务端 completed 时，写入服务端建议的检测间隔（秒） */
static void apply_server_interval(int interval_sec) {
  if (interval_sec <= 0 || !s_interval_mutex)
    return;
  if (interval_sec < INTERVAL_MIN_SEC)
    interval_sec = INTERVAL_MIN_SEC;
  if (interval_sec > INTERVAL_MAX_SEC)
    interval_sec = INTERVAL_MAX_SEC;
  xSemaphoreTake(s_interval_mutex, portMAX_DELAY);
  s_server_interval_sec = interval_sec;
  xSemaphoreGive(s_interval_mutex);
  ESP_LOGI(TAG, "Server interval received: %d sec (pending)", interval_sec);
}

/* monitor 任务消费一次待应用的服务端间隔，返回 -1 表示没有 */
static int consume_server_interval(void) {
  int v = -1;
  if (!s_interval_mutex)
    return v;
  xSemaphoreTake(s_interval_mutex, portMAX_DELAY);
  v = s_server_interval_sec;
  s_server_interval_sec = -1;
  xSemaphoreGive(s_interval_mutex);
  return v;
}

static void monitor_upload_callback(bool success, const char *tick_id_or_key,
                                    void *user_data) {
  ESP_LOGI(TAG, "Upload callback: success=%d, tick=%s", success,
           tick_id_or_key ? tick_id_or_key : "NULL");
  if (!success || !tick_id_or_key) {
    xSemaphoreGive(s_upload_done_sem);
    return;
  }

  if (s_vlm_result_queue) {
    vlm_result_msg_t msg = {0};
    msg.success = true;
    strlcpy(msg.tick_id, tick_id_or_key, sizeof(msg.tick_id));
    if (xQueueSend(s_vlm_result_queue, &msg, 0) != pdTRUE) {
      ESP_LOGW(TAG, "VLM queue full");
    }
  }
  xSemaphoreGive(s_upload_done_sem);
}

/* ------------------------------------------------------------------
 * cam_helper 订阅推帧回调
 * ------------------------------------------------------------------ */
static void monitor_cam_frame_cb(const camera_fb_t *fb, void *user_arg) {
  cam_capture_ctx_t *ctx = (cam_capture_ctx_t *)user_arg;
  if (!ctx || ctx->captured)
    return;

  if (!fb || !fb->buf || fb->len == 0) {
    ESP_LOGE(TAG, "Received invalid FB in callback");
    return;
  }

  /* 情况 A: 驱动输出直接就是 JPEG */
  if (fb->format == PIXFORMAT_JPEG) {
    ctx->jpeg_buf = (uint8_t *)malloc(fb->len);
    if (ctx->jpeg_buf) {
      memcpy(ctx->jpeg_buf, fb->buf, fb->len);
      ctx->jpeg_len = fb->len;
      ctx->captured = true;
    }
  }
  /* 情况 B: 输出为 RGB565 / YUV，转为 RGB888 后调用软编码器导出 JPEG */
  else {
    size_t rgb888_len = fb->width * fb->height * 3;
    // 从 PSRAM 分配临时 RGB888 空间，避免内部 SRAM 不足
    uint8_t *rgb888_buf =
        (uint8_t *)heap_caps_malloc(rgb888_len, MALLOC_CAP_SPIRAM);

    if (rgb888_buf) {
      // 1. 使用 fmt2rgb888 将 RGB565/YUV 统一转换为 RGB888
      if (fmt2rgb888(fb->buf, fb->len, fb->format, rgb888_buf)) {
        jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
        cfg.width = fb->width;
        cfg.height = fb->height;
        cfg.src_type =
            JPEG_PIXEL_FORMAT_RGB888; // 符合 esp_jpeg 要求的编码输入类型
        cfg.quality = 50;
        cfg.task_enable = false;

        jpeg_enc_handle_t enc = NULL;
        if (jpeg_enc_open(&cfg, &enc) == JPEG_ERR_OK) {
          size_t jpg_buf_size = 80 * 1024;
          uint8_t *jpg_buf = (uint8_t *)jpeg_calloc_align(jpg_buf_size, 16);
          if (jpg_buf) {
            int out_len = 0;
            if (jpeg_enc_process(enc, rgb888_buf, rgb888_len, jpg_buf,
                                 jpg_buf_size, &out_len) == JPEG_ERR_OK &&
                out_len > 0) {
              ctx->jpeg_buf = (uint8_t *)malloc(out_len);
              if (ctx->jpeg_buf) {
                memcpy(ctx->jpeg_buf, jpg_buf, out_len);
                ctx->jpeg_len = out_len;
                ctx->captured = true;
              }
            }
            jpeg_free_align(jpg_buf);
          }
          jpeg_enc_close(enc);
        } else {
          ESP_LOGE(TAG, "jpeg_enc_open failed");
        }
      } else {
        ESP_LOGE(TAG, "fmt2rgb888 convert failed");
      }
      heap_caps_free(rgb888_buf);
    } else {
      ESP_LOGE(TAG, "Failed to allocate SPIRAM for RGB888 buffer");
    }
  }

  // 通知抓图主线程：一帧图片已处理完
  xSemaphoreGive(ctx->sem);
}

/* ------------------------------------------------------------------
 * 捕获并入队 (已全套重构适配 cam_helper.h)
 * ------------------------------------------------------------------ */
static bool capture_and_enqueue(void) {
  int32_t device_id = 1;
  nvs_helper_get_i32("storage", "device_id", &device_id);
  int32_t task_id = task_manager_get_active_id();
  if (task_id == 0) {
    ESP_LOGW(TAG, "No active task, skip capture");
    return false;
  }

  cam_capture_ctx_t capture_ctx = {
      .sem = xSemaphoreCreateBinary(),
      .jpeg_buf = NULL,
      .jpeg_len = 0,
      .captured = false,
  };

  if (!capture_ctx.sem) {
    ESP_LOGE(TAG, "Failed to create capture sem");
    return false;
  }

  // 1. 动态订阅摄像头（这会自动使能 / 唤醒 Camera 硬件，增加订阅者计数）
  cam_subscriber_handle_t sub_handle =
      cam_helper_subscribe(monitor_cam_frame_cb, &capture_ctx);
  if (!sub_handle) {
    ESP_LOGE(TAG, "Cam helper subscribe failed");
    vSemaphoreDelete(capture_ctx.sem);
    return false;
  }

  // 2. 等待相机硬件上电完成（冷启动 + 初始化失败重试可能耗时数秒，
  //    不能让启动时间吃掉下面的 3 秒帧超时窗口）
  int64_t powerup_deadline =
      esp_timer_get_time() + (int64_t)CAM_POWERUP_TIMEOUT_MS * 1000LL;
  while (!cam_helper_is_hardware_powered()) {
    if (esp_timer_get_time() > powerup_deadline) {
      ESP_LOGE(TAG, "Camera hardware power-up timeout");
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // 3. 等待摄像头回调推帧并完成编码 (超时 3 秒，只覆盖相机就绪后的推帧)
  //    此时 face detector 已持有订阅一段时间，sensor AE/AWB 已稳定，不会偏绿
  bool wait_ok = xSemaphoreTake(capture_ctx.sem, pdMS_TO_TICKS(3000)) == pdTRUE;

  // 4. 抓取完成/超时后，立刻取消订阅（让 Camera 自动进入 Standby 低功耗）
  cam_helper_unsubscribe(sub_handle);
  vSemaphoreDelete(capture_ctx.sem);

  if (!wait_ok || !capture_ctx.captured || !capture_ctx.jpeg_buf) {
    ESP_LOGE(TAG, "Camera capture or encoding timeout/failed");
    if (capture_ctx.jpeg_buf)
      free(capture_ctx.jpeg_buf);
    return false;
  }

  // 6. 将 JPEG 存入 LittleFS 文件
  char filepath[64];
  int64_t timestamp = esp_timer_get_time() / 1000;
  snprintf(filepath, sizeof(filepath), "/littlefs/monitor_%lld.jpg",
           (long long)timestamp);

  FILE *f = fopen(filepath, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open file for write: %s", filepath);
    free(capture_ctx.jpeg_buf);
    return false;
  }
  fwrite(capture_ctx.jpeg_buf, 1, capture_ctx.jpeg_len, f);
  fclose(f);
  free(capture_ctx.jpeg_buf);

  // 7. 提交上传任务给上传队列
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
    // 队列满 → 等待上传器消费后再试，避免丢弃已捕获的图片
    ESP_LOGW(TAG, "Upload queue full, waiting for drain...");
    for (int wait = 0; wait < 10; wait++) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      if (img_queue_push(&job)) {
        ESP_LOGI(TAG, "Queue drained, job enqueued after %d sec wait",
                 wait + 1);
        return true;
      }
    }
    ESP_LOGE(TAG, "Upload queue still full after 10s, dropping capture");
    remove(filepath);
    return false;
  }
  return true;
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
      vTaskDelay(pdMS_TO_TICKS(VLM_POLL_INTERVAL_MS));
      if (!http_get_json(path, resp, sizeof(resp)))
        continue;

      cJSON *root = cJSON_Parse(resp);
      if (!root)
        continue;

      cJSON *status = cJSON_GetObjectItem(root, "status");
      if (status && status->valuestring) {
        if (strcmp(status->valuestring, "completed") == 0) {
          vlm_ok = true;
          /* 服务端按 anomalyIntegral 算好的下次检测间隔（秒），
           * 交给 monitor 状态机在下一轮 SLEEP 中应用 */
          cJSON *interval_obj = cJSON_GetObjectItem(root, "interval");
          if (interval_obj && cJSON_IsNumber(interval_obj)) {
            apply_server_interval(interval_obj->valueint);
          }
          cJSON_Delete(root);
          break;
        } else if (strcmp(status->valuestring, "pending") != 0) {
          cJSON_Delete(root);
          break;
        }
      }
      cJSON_Delete(root);
    }
    ESP_LOGI(TAG, "VLM poll result for tick %s: %s", msg.tick_id,
             vlm_ok ? "SUCCESS" : "FAILED");
  }
}

static void monitor_task_func(void *arg) {
  ESP_LOGI(TAG, "Monitor task started");
  s_state = MONITOR_STATE_IDLE;
  s_current_interval_sec = INTERVAL_MIN_SEC;

  while (1) {
    /* 暂停期间强制拍照执行完毕（回到 SLEEP 或任务结束进入 IDLE）→ 恢复暂停 */
    if (s_pause_after_capture &&
        (s_state == MONITOR_STATE_SLEEP || task_manager_get_active_id() == 0)) {
      s_pause_after_capture = false;
      s_is_paused = true;
      ESP_LOGI(TAG, "Forced capture done, monitor re-paused");
    }

    if (s_is_paused) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    int32_t active_task = task_manager_get_active_id();
    if (active_task == 0) {
      if (s_state != MONITOR_STATE_IDLE) {
        ESP_LOGI(TAG, "No active task, enter IDLE");
        if (s_state == MONITOR_STATE_WAIT_FACE &&
            face_detector_helper_is_running()) {
          face_detector_helper_stop_continuous();
        }
        s_state = MONITOR_STATE_IDLE;
        s_current_interval_sec = INTERVAL_MIN_SEC;
        (void)consume_server_interval(); // 任务结束，丢弃待应用的服务端间隔
      }
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }

    int64_t now_us = esp_timer_get_time();
    switch (s_state) {
    case MONITOR_STATE_IDLE:
      s_current_interval_sec = INTERVAL_MIN_SEC;
      s_next_wake_time_us =
          now_us + (int64_t)s_current_interval_sec * 1000000LL;
      s_state = MONITOR_STATE_SLEEP;
      ESP_LOGI(TAG, "IDLE->SLEEP, interval=%d sec", s_current_interval_sec);
      break;

    case MONITOR_STATE_SLEEP: {
      /* 服务端动态间隔到达 → 覆盖本地间隔并重新计时 */
      int pending = consume_server_interval();
      if (pending > 0) {
        s_current_interval_sec = pending;
        s_next_wake_time_us =
            esp_timer_get_time() + (int64_t)s_current_interval_sec * 1000000LL;
        ESP_LOGI(TAG, "Server interval applied, next wake in %d sec",
                 s_current_interval_sec);
      }
      if (now_us >= s_next_wake_time_us) {
        ESP_LOGI(TAG, "Interval reached -> WAIT_FACE");
        s_state = MONITOR_STATE_WAIT_FACE;
        s_face_wait_start_us = now_us;
      } else {
        int64_t remain_ms = (s_next_wake_time_us - now_us) / 1000;
        vTaskDelay(pdMS_TO_TICKS((remain_ms > 1000) ? 1000 : remain_ms));
      }
      break;
    }

    case MONITOR_STATE_WAIT_FACE:
      if (!face_detector_helper_is_running()) {
        face_detector_helper_start_continuous();
        // start_continuous 内部已清空旧缓存；首帧到达前 get_results 返回
        // count=0 这也是 camera sensor 的预热同步点：face detector
        // 订阅了摄像头， 等它处理完首帧，sensor 的 AE/AWB 也已稳定
        s_face_wait_start_us = esp_timer_get_time();
      }

      face_detect_results_t results = {0};
      face_detector_helper_get_results(&results);

      if (results.count > 0) {
        int64_t wait_sec =
            (esp_timer_get_time() - s_face_wait_start_us) / 1000000LL;
        ESP_LOGI(TAG, "Face detected after %lld sec -> CAPTURING", wait_sec);
        face_detector_helper_stop_continuous();
        s_state = MONITOR_STATE_CAPTURING;
      } else {
        int64_t elapsed_sec =
            (esp_timer_get_time() - s_face_wait_start_us) / 1000000LL;
        if (elapsed_sec >= FACE_WAIT_TIMEOUT_SEC) {
          ESP_LOGW(TAG, "Face wait timeout, skip cycle");

          face_detector_helper_stop_continuous();
          s_current_interval_sec =
              adjust_interval_by_face_wait(FACE_WAIT_SLOW_SEC);
          s_next_wake_time_us = esp_timer_get_time() +
                                (int64_t)s_current_interval_sec * 1000000LL;
          s_state = MONITOR_STATE_SLEEP;
        } else {
          vTaskDelay(pdMS_TO_TICKS(FACE_POLL_INTERVAL_MS));
        }
      }
      break;

    case MONITOR_STATE_CAPTURING: {
      int64_t face_wait_sec =
          (esp_timer_get_time() - s_face_wait_start_us) / 1000000LL;

      /* 本轮抓拍已开始执行：消费挂起的强制请求标记，避免本轮上传等待
       * 结束后又被同一标记触发一次重复抓拍 */
      s_force_capture_pending = false;

      bool ok = capture_and_enqueue();
      if (!ok) {
        ESP_LOGE(TAG, "Capture failed");
        s_current_interval_sec += INTERVAL_STEP_SEC;
        if (s_current_interval_sec > INTERVAL_MAX_SEC)
          s_current_interval_sec = INTERVAL_MAX_SEC;
        s_next_wake_time_us =
            esp_timer_get_time() + (int64_t)s_current_interval_sec * 1000000LL;
        s_state = MONITOR_STATE_SLEEP;
        break;
      }

      xSemaphoreTake(s_upload_done_sem, 0);

      /* 上传回调等待改为 1s 分片轮询：期间到达的强制拍照请求可随时打断。
       * 原一次性阻塞 30s 时，force_capture 只能改状态变量，任务卡在旧
       * 循环里，等待结束后仍无条件进 SLEEP，强制请求被静默丢弃。 */
      bool upload_done = false;
      for (int waited_ms = 0; waited_ms < UPLOAD_CALLBACK_TIMEOUT_MS;
           waited_ms += 1000) {
        if (s_force_capture_pending)
          break;
        if (xSemaphoreTake(s_upload_done_sem, pdMS_TO_TICKS(1000)) == pdTRUE) {
          upload_done = true;
          break;
        }
      }

      if (upload_done) {
        s_current_interval_sec = adjust_interval_by_face_wait(face_wait_sec);
      } else if (s_force_capture_pending) {
        ESP_LOGI(TAG, "Upload wait aborted by forced capture");
      } else {
        ESP_LOGW(TAG, "Upload timeout");
        s_current_interval_sec += INTERVAL_STEP_SEC;
        if (s_current_interval_sec > INTERVAL_MAX_SEC)
          s_current_interval_sec = INTERVAL_MAX_SEC;
      }

      /* 等待期间有强制拍照请求 → 不进入 SLEEP，立即执行下一轮抓拍 */
      if (s_force_capture_pending) {
        s_force_capture_pending = false;
        s_state = MONITOR_STATE_CAPTURING;
        ESP_LOGI(TAG, "Pending forced capture executed immediately");
        break;
      }

      s_next_wake_time_us =
          esp_timer_get_time() + (int64_t)s_current_interval_sec * 1000000LL;
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

void monitor_task_force_capture(bool skip_face) {
  if (s_is_paused) {
    /* 暂停期间（调试界面）强制上传：临时解除暂停执行一轮，
     * monitor 任务在拍照完成回到 SLEEP/IDLE 后自动重新暂停 */
    ESP_LOGI(TAG, "Monitor paused, temporarily unpausing for forced capture");
    s_pause_after_capture = true;
    s_is_paused = false;
  }

  xSemaphoreTake(s_monitor_mutex, portMAX_DELAY);

  /* 挂起标记：若任务正阻塞在上传回调等待里，标记能让等待分片轮询
   * 提前退出，并在本轮结束后立即执行强制抓拍（否则请求被旧循环吞掉） */
  s_force_capture_pending = true;

  monitor_state_t prev_state = s_state;
  int64_t prev_wake = s_next_wake_time_us;
  int prev_interval = s_current_interval_sec;

  if (skip_face) {
    ESP_LOGI(TAG, "Force capture (skip face) -> CAPTURING, prev_state=%d",
             prev_state);
    s_state = MONITOR_STATE_CAPTURING;
  } else {
    ESP_LOGI(TAG, "Force capture (with face) -> WAIT_FACE, prev_state=%d",
             prev_state);
    // Stop face detector if running so it starts fresh
    if (face_detector_helper_is_running()) {
      face_detector_helper_stop_continuous();
    }
    s_state = MONITOR_STATE_WAIT_FACE;
    s_face_wait_start_us = esp_timer_get_time();
  }

  // Reset interval so next normal cycle starts from min
  s_current_interval_sec = INTERVAL_MIN_SEC;

  xSemaphoreGive(s_monitor_mutex);
  (void)consume_server_interval(); // 强制拍照走本地调度，丢弃待应用的服务端间隔
  (void)prev_state;
  (void)prev_wake;
  (void)prev_interval;
}

void monitor_task_start(void) {
  if (!s_upload_done_sem)
    s_upload_done_sem = xSemaphoreCreateBinary();
  if (!s_monitor_mutex)
    s_monitor_mutex = xSemaphoreCreateMutex();
  if (!s_interval_mutex)
    s_interval_mutex = xSemaphoreCreateMutex();
  if (!s_vlm_result_queue)
    s_vlm_result_queue = xQueueCreate(5, sizeof(vlm_result_msg_t));

  if (!s_vlm_poll_task_handle) {
    size_t stack_words = VLM_POLL_STACK_SIZE / sizeof(StackType_t);
    s_vlm_task_stack = (StackType_t *)heap_caps_malloc(
        VLM_POLL_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (s_vlm_task_stack) {
      s_vlm_poll_task_handle = xTaskCreateStaticPinnedToCore(
          vlm_poll_task_func, "vlm_poll", stack_words, NULL, 2,
          s_vlm_task_stack, &s_vlm_task_tcb, 1);
    } else {
      xTaskCreatePinnedToCore(vlm_poll_task_func, "vlm_poll",
                              VLM_POLL_STACK_SIZE, NULL, 2,
                              &s_vlm_poll_task_handle, 1);
    }
  }

  if (!s_monitor_task_handle) {
    /* 任务栈从 PSRAM 分配（与 vlm_poll/uploader 一致）：
     * 启动后内部 RAM 仅剩约 10KB，xTaskCreatePinnedToCore 的 8KB 内部栈
     * 分配经常失败且返回值被忽略 → 任务静默不存在（状态机完全不运行，
     * 表现为调试页强制拍照无任何反应）。 */
    size_t stack_words = MONITOR_STACK_SIZE / sizeof(StackType_t);
    s_monitor_task_stack = (StackType_t *)heap_caps_malloc(
        MONITOR_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_monitor_task_stack) {
      s_monitor_task_handle = xTaskCreateStaticPinnedToCore(
          monitor_task_func, "monitor", stack_words, NULL, 3,
          s_monitor_task_stack, &s_monitor_task_tcb, 1);
    }
    if (!s_monitor_task_handle) {
      ESP_LOGE(TAG, "Failed to allocate/create monitor task");
    }
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
    (void)consume_server_interval(); // 外部重置调度，丢弃待应用的服务端间隔
    ESP_LOGI(TAG, "Timer reset with 1s delay before next wake");
  }
}

void monitor_task_pause(void) {
  ESP_LOGI(TAG, "Monitor task pause requested");
  s_is_paused = true;
}

void monitor_task_resume(void) {
  ESP_LOGI(TAG, "Monitor task resume requested");
  s_pause_after_capture = false; /* 用户主动恢复，取消待执行的重新暂停 */
  monitor_task_reset_timer();
  s_is_paused = false;
}