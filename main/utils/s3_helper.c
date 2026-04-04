#include "s3_helper.h"
#include "cJSON.h"
#include "esp_heap_caps.h" // 引入 PSRAM 支持
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "http_client_helper.h"
#include "img_stack.h"
#include "nvs_helper.h"
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEVICE_ID 1
static const char *TAG = "S3_helper";

#define MAX_RETRY_LIMIT 5     // 最大重试次数
#define RETRY_BACKOFF_MS 5000 // 失败后的回退时间

static void rewrite_mdns_url(const char *original_url, char *out_url,
                             size_t out_len, const char *ip) {
  const char *mdns_host = "aobara-pc.local";
  const char *pos = strstr(original_url, mdns_host);

  if (pos && ip && strlen(ip) > 0) {
    size_t prefix_len = pos - original_url;
    // 拼接: 协议部分 + IP + 路径部分
    snprintf(out_url, out_len, "%.*s%s%s", (int)prefix_len, original_url, ip,
             pos + strlen(mdns_host));
    ESP_LOGI(TAG, "已接收预签名URL，拼接后的URL:%s", out_url);
  } else {
    // 如果没找到域名或 IP 无效，原样拷贝
    strlcpy(out_url, original_url, out_len);
  }
}

static void uploader_task(void *arg) {
  while (1) {
    img_job_t job;
    // 1. Peek 任务
    if (!img_stack_peek(&job)) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    // --- 核心重试逻辑开始 ---
    if (job.retry_count >= MAX_RETRY_LIMIT) {
      ESP_LOGE(TAG, "Job %s reached max retries (%d). Dropping.", job.path,
               MAX_RETRY_LIMIT);
      if (job.ctx) {
        job.ctx->is_finished = true;
        job.ctx->success = false;
      }
      remove(job.path);   // 即使上传失败，达到上限也要清理文件防止占满磁盘
      img_stack_commit(); // 弹出并丢弃该坏任务
      continue;
    }
    // --- 核心重试逻辑结束 ---

    int32_t deviceId = DEVICE_ID;
    nvs_helper_get_i32("storage", "device_id", &deviceId);

    char url[128];
    snprintf(url, sizeof(url), "/device/%ld/upload-url", deviceId);

    // 注意：由于 resp 之前发生过 Overflow，建议调大到 1024
    char resp[1024];

    // 步骤 A: 获取 URL
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

    // 步骤 B: 读取文件并上传
    FILE *f = fopen(job.path, "rb");
    if (!f) {
      ESP_LOGE(TAG, "File missing: %s", job.path);
      cJSON_Delete(root);
      img_stack_commit(); // 文件不存在，重试无意义，直接移除
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

    // 步骤 C: 服务器确认
    cJSON *post = cJSON_CreateObject();
    cJSON_AddNumberToObject(post, "deviceId", deviceId);
    cJSON_AddNumberToObject(post, "taskId", job.task_id);
    cJSON_AddStringToObject(post, "imageKey", imageKey);

    char *json_str = cJSON_PrintUnformatted(post);
    bool post_success = http_post_json("/device/image", json_str);

    cJSON_Delete(post);
    free(json_str);
    cJSON_Delete(root);

    if (post_success) {
      if (job.ctx) {
        strlcpy(job.ctx->image_key, imageKey, sizeof(job.ctx->image_key));
        job.ctx->success = true;
        job.ctx->is_finished = true;
      }
      remove(job.path);
      img_stack_commit(); // 成功完成
      ESP_LOGI(TAG, "Job success.");
    } else {
      goto task_fail_handle;
    }

    continue;

  task_fail_handle:

    job.retry_count++;
    img_stack_update_retry(&job);

    ESP_LOGW(TAG, "Job failed, retry count: %d/%d", job.retry_count,
             MAX_RETRY_LIMIT);
    vTaskDelay(pdMS_TO_TICKS(RETRY_BACKOFF_MS));
  }
}

void uploader_task_start(void) {
  // 根据实际情况可能需要调大堆栈
  xTaskCreate(uploader_task, "uploader", 8192, NULL, 5, NULL);
}