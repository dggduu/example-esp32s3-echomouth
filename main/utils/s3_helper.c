#include "s3_helper.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "http_client_helper.h"
#include "img_queue.h"
#include "nvs_helper.h"
#include "sdkconfig.h"
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

#ifdef CONFIG_HTTP_MODE_MDNS
static void rewrite_mdns_url(const char *original_url, char *out_url,
                             size_t out_len, const char *ip) {
  const char *mdns_host = CONFIG_SERVER_HOSTNAME ".local";
  const char *pos = strstr(original_url, mdns_host);

  if (pos && ip && strlen(ip) > 0) {
    size_t prefix_len = pos - original_url;
    snprintf(out_url, out_len, "%.*s%s%s", (int)prefix_len, original_url, ip,
             pos + strlen(mdns_host));
    ESP_LOGI(TAG, "Rewritten pre-signed URL: %s", out_url);
  } else {
    strlcpy(out_url, original_url, out_len);
  }
}
#endif

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

    // 1. 获取预签名 URL
    char url[128];
    snprintf(url, sizeof(url), "/device/%d/upload-url", (int)device_id);
    char resp[1024];
    if (!http_get_json(url, resp, sizeof(resp))) {
      goto fail_retry;
    }

    cJSON *root = cJSON_Parse(resp);
    if (!root) {
      goto fail_retry;
    }

    cJSON *url_obj = cJSON_GetObjectItem(root, "uploadUrl");
    cJSON *key_obj = cJSON_GetObjectItem(root, "imageKey");
    if (!url_obj || !url_obj->valuestring || !key_obj ||
        !key_obj->valuestring) {
      cJSON_Delete(root);
      goto fail_retry;
    }

    char raw_upload_url[512];
    char image_key[128];
    strlcpy(raw_upload_url, url_obj->valuestring, sizeof(raw_upload_url));
    strlcpy(image_key, key_obj->valuestring, sizeof(image_key));
    cJSON_Delete(root); // 及时释放 HTTP 响应的 JSON 解析树
    root = NULL;

    char final_upload_url[512];
#ifdef CONFIG_HTTP_MODE_MDNS
    char ip[32] = {0};
    if (get_mdns_server_ip(ip, sizeof(ip))) {
      rewrite_mdns_url(raw_upload_url, final_upload_url,
                       sizeof(final_upload_url), ip);
    } else {
      strlcpy(final_upload_url, raw_upload_url, sizeof(final_upload_url));
    }
#else
    strlcpy(final_upload_url, raw_upload_url, sizeof(final_upload_url));
#endif

    // 2. 读取二进制图片文件
    FILE *f = fopen(job.path, "rb");
    if (!f) {
      ESP_LOGE(TAG, "File missing: %s", job.path);
      img_queue_commit();
      continue;
    }

    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!buf) {
      ESP_LOGE(TAG, "SPIRAM OOM for size: %d", size);
      fclose(f);
      goto fail_retry;
    }
    fread(buf, 1, size, f);
    fclose(f);

    // 3. PUT 上传 S3
    bool put_success = http_put_binary(final_upload_url, buf, size);
    heap_caps_free(buf);
    if (!put_success) {
      goto fail_retry;
    }

    // 4. 上报后端
    bool post_success = false;
    char tick_id[64] = {0};

    if (job.type == IMG_TYPE_MONITOR) {
      cJSON *post = cJSON_CreateObject();
      cJSON_AddNumberToObject(post, "deviceId", device_id);
      if (task_id > 0) {
        cJSON_AddNumberToObject(post, "taskId", task_id);
      }
      cJSON_AddStringToObject(post, "imageKey", image_key);

      char *json_str = cJSON_PrintUnformatted(post);
      cJSON_Delete(post);

      char resp_body[512];
      bool http_ok = false;
      if (json_str) {
        http_ok = http_post_json_with_response("/device/image", json_str,
                                               resp_body, sizeof(resp_body));
        free(json_str);
      }

      if (http_ok) {
        cJSON *resp_root = cJSON_Parse(resp_body);
        if (resp_root) {
          cJSON *tick_obj = cJSON_GetObjectItem(resp_root, "tickId");
          if (tick_obj && tick_obj->valuestring) {
            strlcpy(tick_id, tick_obj->valuestring, sizeof(tick_id));
            post_success = true;
          }
          cJSON_Delete(resp_root);
        }
      }
    } else {
      cJSON *post = cJSON_CreateObject();
      cJSON_AddNumberToObject(post, "deviceId", device_id);
      if (task_id > 0) {
        cJSON_AddNumberToObject(post, "taskId", task_id);
      }
      cJSON_AddStringToObject(post, "imageKey", image_key);
      char *json_str = cJSON_PrintUnformatted(post);
      cJSON_Delete(post);

      if (json_str) {
        post_success = http_post_json("/device/image/result", json_str);
        free(json_str);
      }
    }

    if (!post_success) {
      goto fail_retry;
    }

    // 处理成功，清理本地文件并通知
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

  s_uploader_task_stack = (StackType_t *)heap_caps_malloc(
      UPLOADER_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_uploader_task_stack == NULL) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM stack for uploader task");
    return;
  }

  size_t stack_depth = UPLOADER_STACK_SIZE / sizeof(StackType_t);

  s_uploader_task_handle = xTaskCreateStaticPinnedToCore(
      uploader_task, "uploader", stack_depth, NULL, 6, s_uploader_task_stack,
      &s_uploader_task_tcb, 0);

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