#include "s3_helper.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "http_client_helper.h"
#include "img_queue.h" // 替换为新的队列接口
#include "nvs_helper.h"
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEVICE_ID 1
static const char *TAG = "S3_helper";

#define MAX_RETRY_LIMIT 5
#define RETRY_BACKOFF_MS 5000

// 前向声明
static void rewrite_mdns_url(const char *original_url, char *out_url,
                             size_t out_len, const char *ip);

// 上传任务类型
typedef enum {
  UPLOAD_TYPE_MONITOR = 0, // 定时监控，调用 /device/image
  UPLOAD_TYPE_MANUAL = 1   // 手动拍照，调用 /device/image/result
} upload_type_t;

// 扩展 img_job_t 中的 user_data，用于传递额外参数
typedef struct {
  upload_type_t type; // 上传类型
  int32_t device_id;
  int32_t task_id;
  // 手动任务可能需要回调
  void *callback_ctx; // 指向 cam_shared_ctx_t
} upload_user_ctx_t;

static void uploader_task(void *arg) {
  while (1) {
    img_job_t job;
    if (!img_queue_peek(&job)) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    // 重试超限处理
    if (job.retry_count >= MAX_RETRY_LIMIT) {
      ESP_LOGE(TAG, "Job %s reached max retries (%d). Dropping.", job.path,
               MAX_RETRY_LIMIT);
      // 调用失败回调
      if (job.on_complete) {
        job.on_complete(false, NULL, job.user_data);
      }
      remove(job.path);
      img_queue_commit();
      continue;
    }

    // 解析用户上下文
    upload_user_ctx_t *ctx = (upload_user_ctx_t *)job.user_data;
    if (!ctx) {
      ESP_LOGE(TAG, "Missing user context, dropping job");
      remove(job.path);
      img_queue_commit();
      continue;
    }

    int32_t deviceId = ctx->device_id;
    int32_t taskId = ctx->task_id;
    upload_type_t type = ctx->type;

    // 步骤 A: 获取预签名 URL
    char url[128];
    snprintf(url, sizeof(url), "/device/%ld/upload-url", deviceId);

    char resp[1024];
    if (!http_get_json(url, resp, sizeof(resp))) {
      goto task_fail_handle;
    }

    cJSON *root = cJSON_Parse(resp);
    if (!root)
      goto task_fail_handle;

    cJSON *url_obj = cJSON_GetObjectItem(root, "uploadUrl");
    cJSON *key_obj = cJSON_GetObjectItem(root, "imageKey");
    if (!url_obj || !key_obj) {
      cJSON_Delete(root);
      goto task_fail_handle;
    }

    char *rawUploadUrl = url_obj->valuestring;
    char *imageKey = key_obj->valuestring;

    char ip[32] = {0};
    char finalUploadUrl[1024];
    if (resolve_server_ip(ip, sizeof(ip))) {
      rewrite_mdns_url(rawUploadUrl, finalUploadUrl, sizeof(finalUploadUrl),
                       ip);
      ESP_LOGI(TAG, "Rewritten URL: %s", finalUploadUrl);
    } else {
      strlcpy(finalUploadUrl, rawUploadUrl, sizeof(finalUploadUrl));
    }

    // 步骤 B: 读取文件并上传到 S3
    FILE *f = fopen(job.path, "rb");
    if (!f) {
      ESP_LOGE(TAG, "File missing: %s", job.path);
      cJSON_Delete(root);
      img_queue_commit(); // 文件不存在，直接丢弃
      continue;
    }

    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!buf) {
      ESP_LOGE(TAG, "SPIRAM OOM");
      fclose(f);
      cJSON_Delete(root);
      vTaskDelay(pdMS_TO_TICKS(RETRY_BACKOFF_MS));
      continue;
    }

    fread(buf, 1, size, f);
    fclose(f);

    bool put_success = http_put_binary(finalUploadUrl, buf, size);
    heap_caps_free(buf);

    if (!put_success) {
      cJSON_Delete(root);
      goto task_fail_handle;
    }

    // 步骤 C: 根据类型调用不同的服务器接口
    bool post_success = false;
    char tickId[32] = {0};

    if (type == UPLOAD_TYPE_MONITOR) {
      // 定时监控：调用 /device/image
      cJSON *post = cJSON_CreateObject();
      cJSON_AddNumberToObject(post, "deviceId", deviceId);
      if (taskId > 0) {
        cJSON_AddNumberToObject(post, "taskId", taskId);
      }
      cJSON_AddStringToObject(post, "imageKey", imageKey);
      // 使用当前时间作为 timestamp，同时也是 tickId 的基础
      int64_t now_ms = esp_timer_get_time() / 1000;
      cJSON_AddNumberToObject(post, "timestamp", now_ms);
      snprintf(tickId, sizeof(tickId), "%lld", now_ms);

      char *json_str = cJSON_PrintUnformatted(post);
      post_success = http_post_json("/device/image", json_str);
      cJSON_Delete(post);
      free(json_str);
    } else {
      // 手动拍照：调用 /device/image/result
      cJSON *post = cJSON_CreateObject();
      cJSON_AddNumberToObject(post, "deviceId", deviceId);
      if (taskId > 0) {
        cJSON_AddNumberToObject(post, "taskId", taskId);
      }
      cJSON_AddStringToObject(post, "imageKey", imageKey);
      cJSON_AddNumberToObject(post, "timestamp", esp_timer_get_time() / 1000);

      char *json_str = cJSON_PrintUnformatted(post);
      post_success = http_post_json("/device/image/result", json_str);
      cJSON_Delete(post);
      free(json_str);
    }

    cJSON_Delete(root);

    if (!post_success) {
      goto task_fail_handle;
    }

    // 成功：处理回调
    if (type == UPLOAD_TYPE_MONITOR) {
      // 对于监控任务，可能需要轮询 interval，但这里我们简单认为成功即可
      // 你可以将 tickId 通过某种方式传给监控任务进行轮询
      // 这里为了简化，我们只调用 on_complete 并传递 imageKey
      if (job.on_complete) {
        job.on_complete(true, imageKey, job.user_data);
      }
    } else {
      // 手动任务：直接成功
      if (job.on_complete) {
        job.on_complete(true, imageKey, job.user_data);
      }
    }

    remove(job.path);
    img_queue_commit();
    ESP_LOGI(TAG, "Job success, type: %d", type);
    continue;

  task_fail_handle:
    job.retry_count++;
    img_queue_update_retry(job.retry_count);
    ESP_LOGW(TAG, "Job failed, retry count: %d/%d", job.retry_count,
             MAX_RETRY_LIMIT);
    vTaskDelay(pdMS_TO_TICKS(RETRY_BACKOFF_MS));
  }
}

void uploader_task_start(void) {
  xTaskCreate(uploader_task, "uploader", 8192, NULL, 5, NULL);
}