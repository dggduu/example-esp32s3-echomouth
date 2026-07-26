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

#define MAX_RETRY_LIMIT 10
#define RETRY_BACKOFF_MS 5000
#define RETRY_BACKOFF_LONG_MS 30000
#define UPLOADER_STACK_SIZE 6144

static TaskHandle_t s_uploader_task_handle = NULL;
static StaticTask_t s_uploader_task_tcb;
static StackType_t *s_uploader_task_stack = NULL;

/**
 * @brief 提取 URL 中的 host[:port]，用于 S3 签名 Host 覆写
 * 例: "http://aobara-pc.local:9000/bucket/key" → "aobara-pc.local:9000"
 */
static bool extract_host_from_url(const char *url, char *host_out,
                                  size_t host_len) {
  if (!url || !host_out || host_len == 0)
    return false;
  const char *start = strstr(url, "://");
  if (!start)
    return false;
  start += 3;
  const char *end = strchr(start, '/');
  if (!end)
    return false;
  size_t len = end - start;
  if (len >= host_len)
    len = host_len - 1;
  memcpy(host_out, start, len);
  host_out[len] = '\0';
  return true;
}

static void uploader_task(void *arg) {
  while (1) {
    img_job_t job;
    if (!img_queue_peek(&job)) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    // 离线等待：网络不通时不消耗重试次数，等待恢复后自动冲传
    if (!img_queue_is_network_up()) {
      ESP_LOGW(TAG, "Network down, waiting for recovery...");
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    if (job.retry_count >= MAX_RETRY_LIMIT) {
      // 达到最大重试次数 → 保留文件，等待网络恢复后重新开始计数
      ESP_LOGW(TAG, "Job %s reached max retries (%d). Holding for network recovery.",
               job.path, MAX_RETRY_LIMIT);
      vTaskDelay(pdMS_TO_TICKS(15000)); // 等待 15 秒，期望网络恢复
      job.retry_count = 0;              // 重置计数器
      img_queue_update_retry(0);
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

    // 提取原始 Host（用于 S3 签名匹配）
    char original_host[64] = {0};
    extract_host_from_url(raw_upload_url, original_host, sizeof(original_host));

    // URL 改写：mDNS 主机名 → IP（esp_http_client 不支持自动 mDNS）
    char final_upload_url[512];
    char override_host[64] = {0};
#ifdef CONFIG_HTTP_MODE_MDNS
    char ip[32] = {0};
    if (get_mdns_server_ip(ip, sizeof(ip)) && original_host[0] != '\0') {
      // 替换 URL 中的 host 为 IP
      const char *host_start = strstr(raw_upload_url, "://");
      if (host_start) {
        host_start += 3;
        const char *host_end = strchr(host_start, '/');
        size_t prefix_len = host_start - raw_upload_url;
        snprintf(final_upload_url, sizeof(final_upload_url), "%.*s%s%s",
                 (int)prefix_len, raw_upload_url, ip, host_end);
        // 覆写 Host 头为原始 mDNS 主机名，匹配 S3 预签名
        strlcpy(override_host, original_host, sizeof(override_host));
        ESP_LOGI(TAG, "URL rewritten: %s -> %s (Host: %s)", original_host, ip,
                 override_host);
      } else {
        strlcpy(final_upload_url, raw_upload_url, sizeof(final_upload_url));
      }
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

    // 3. PUT 上传 S3（Host 覆写以匹配签名）
    // const char *host_to_use =
    //     override_host[0] != '\0' ? override_host : NULL;
    // bool put_success =
    //     http_put_binary_with_host(final_upload_url, buf, size, host_to_use);
    bool put_success = http_put_binary(raw_upload_url, buf, size);
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
    // 自适应退避：前 3 次短间隔，之后长间隔等待网络恢复
    int backoff = (job.retry_count <= 3) ? RETRY_BACKOFF_MS : RETRY_BACKOFF_LONG_MS;
    ESP_LOGW(TAG, "Job %s failed, retry %d/%d (backoff %d ms)",
             job.path, job.retry_count, MAX_RETRY_LIMIT, backoff);
    vTaskDelay(pdMS_TO_TICKS(backoff));
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