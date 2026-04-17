#include "s3_helper.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "http_client_helper.h"
#include "img_queue.h"
#include "nvs_helper.h"
#include "time_test_helper.h"
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEVICE_ID 1
static const char *TAG = "S3_helper";

#define MAX_RETRY_LIMIT 5
#define RETRY_BACKOFF_MS 5000
#define UPLOADER_STACK_SIZE 6144

static TaskHandle_t s_uploader_task_handle = NULL;
static StaticTask_t s_uploader_task_tcb;
static StackType_t *s_uploader_task_stack = NULL;

static void rewrite_mdns_url(const char *original_url, char *out_url,
                             size_t out_len, const char *ip) {
  const char *mdns_host = "aobara-pc.local";
  const char *pos = strstr(original_url, mdns_host);

  if (pos && ip && strlen(ip) > 0) {
    size_t prefix_len = pos - original_url;
    snprintf(out_url, out_len, "%.*s%s%s", (int)prefix_len, original_url, ip,
             pos + strlen(mdns_host));
    ESP_LOGI(TAG, "已接收预签名URL，拼接后的URL:%s", out_url);
  } else {
    strlcpy(out_url, original_url, out_len);
  }
}

static int64_t get_timestamp_ms(void) {
  time_t now = time(NULL);
  if (now > 1600000000) {
    return (int64_t)now * 1000;
  } else {
    int64_t fallback = esp_timer_get_time() / 1000;
    ESP_LOGW(TAG, "NTP not synced, using fallback timestamp: %lld", fallback);
    return fallback;
  }
}

static void uploader_task(void *arg) {
  while (1) {
    img_job_t job;
    if (!img_queue_peek(&job)) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    if (job.retry_count >= MAX_RETRY_LIMIT) {
      ESP_LOGE(TAG, "Job %s reached max retries (%d). Dropping.", job.path,
               MAX_RETRY_LIMIT);
      if (job.on_complete) {
        job.on_complete(false, NULL, job.user_data);
      }
      remove(job.path);
      img_queue_commit();
      continue;
    }

    int32_t device_id = DEVICE_ID;
    nvs_helper_get_i32("storage", "device_id", &device_id);
    int32_t task_id = job.task_id;

    // 获取预签名 URL
    char url[128];
    snprintf(url, sizeof(url), "/device/%ld/upload-url", device_id);
    char resp[1024];
    if (!http_get_json(url, resp, sizeof(resp))) {
      goto fail_retry;
    }

    cJSON *root = cJSON_Parse(resp);
    if (!root)
      goto fail_retry;
    cJSON *url_obj = cJSON_GetObjectItem(root, "uploadUrl");
    cJSON *key_obj = cJSON_GetObjectItem(root, "imageKey");
    if (!url_obj || !key_obj) {
      cJSON_Delete(root);
      goto fail_retry;
    }

    char *raw_upload_url = url_obj->valuestring;
    char *image_key = key_obj->valuestring;

    char ip[32] = {0};
    char final_upload_url[1024];
    if (resolve_server_ip(ip, sizeof(ip))) {
      rewrite_mdns_url(raw_upload_url, final_upload_url,
                       sizeof(final_upload_url), ip);
    } else {
      strlcpy(final_upload_url, raw_upload_url, sizeof(final_upload_url));
    }

    // 读取并上传文件
    FILE *f = fopen(job.path, "rb");
    if (!f) {
      ESP_LOGE(TAG, "File missing: %s", job.path);
      cJSON_Delete(root);
      img_queue_commit();
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
      job.retry_count++;
      img_queue_update_retry(job.retry_count);
      vTaskDelay(pdMS_TO_TICKS(RETRY_BACKOFF_MS));
      continue;
    }
    fread(buf, 1, size, f);
    fclose(f);

    bool put_success = http_put_binary(final_upload_url, buf, size);
    heap_caps_free(buf);
    if (!put_success) {
      cJSON_Delete(root);
      goto fail_retry;
    }

    // 调用服务器接口
    bool post_success = false;
    char tick_id[64] = {0};

    if (job.type == IMG_TYPE_MONITOR) {
      cJSON *post = cJSON_CreateObject();
      cJSON_AddNumberToObject(post, "deviceId", device_id);
      if (task_id > 0)
        cJSON_AddNumberToObject(post, "taskId", task_id);
      cJSON_AddStringToObject(post, "imageKey", image_key);
      // int64_t now_ms = get_reliable_timestamp_ms();
      // cJSON_AddNumberToObject(post, "timestamp", now_ms);

      char *json_str = cJSON_PrintUnformatted(post);
      char resp_body[512];
      bool http_ok = http_post_json_with_response("/device/image", json_str,
                                                  resp_body, sizeof(resp_body));
      free(json_str);
      cJSON_Delete(post);

      if (!http_ok)
        goto fail_retry;

      // 解析响应中的 tickId
      cJSON *resp_root = cJSON_Parse(resp_body);
      if (resp_root) {
        cJSON *tick_obj = cJSON_GetObjectItem(resp_root, "tickId");
        if (tick_obj && tick_obj->valuestring) {
          strlcpy(tick_id, tick_obj->valuestring, sizeof(tick_id));
          post_success = true;
        }
        cJSON_Delete(resp_root);
      }
      if (!post_success)
        goto fail_retry;
    } else {
      cJSON *post = cJSON_CreateObject();
      cJSON_AddNumberToObject(post, "deviceId", device_id);
      if (task_id > 0)
        cJSON_AddNumberToObject(post, "taskId", task_id);
      cJSON_AddStringToObject(post, "imageKey", image_key);
      char *json_str = cJSON_PrintUnformatted(post);
      post_success = http_post_json("/device/image/result", json_str);
      cJSON_Delete(post);
      free(json_str);
    }

    cJSON_Delete(root);
    if (!post_success)
      goto fail_retry;

    // 成功处理
    remove(job.path);
    img_queue_commit();
    if (job.on_complete) {
      const char *cb_data =
          (job.type == IMG_TYPE_MONITOR) ? tick_id : image_key;
      job.on_complete(true, cb_data, job.user_data);
    }
    ESP_LOGI(TAG, "Job success, type: %d", job.type);
    continue;

  fail_retry:
    job.retry_count++;
    img_queue_update_retry(job.retry_count);
    ESP_LOGW(TAG, "Job failed, retry %d/%d", job.retry_count, MAX_RETRY_LIMIT);
    vTaskDelay(pdMS_TO_TICKS(RETRY_BACKOFF_MS));
  }
}

void uploader_task_start(void) {
  if (s_uploader_task_handle != NULL)
    return;

  // 从 PSRAM 分配栈
  s_uploader_task_stack = (StackType_t *)heap_caps_malloc(
      UPLOADER_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_uploader_task_stack == NULL) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM stack for uploader task");
    return;
  }

  size_t stack_depth = UPLOADER_STACK_SIZE / sizeof(StackType_t);
  s_uploader_task_handle =
      xTaskCreateStatic(uploader_task, "uploader", stack_depth, NULL, 5,
                        s_uploader_task_stack, &s_uploader_task_tcb);

  if (s_uploader_task_handle == NULL) {
    ESP_LOGE(TAG, "Failed to create uploader task");
    heap_caps_free(s_uploader_task_stack);
    s_uploader_task_stack = NULL;
    return;
  }

  ESP_LOGI(TAG, "Uploader task created with PSRAM stack, size %d bytes",
           UPLOADER_STACK_SIZE);
  TEST_MEM_INFO(TAG);
}