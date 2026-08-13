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
/* esp_http_client_perform + URL 拼接局部变量占用较大，且任务栈位于 PSRAM，
 * 溢出会直接踩坏相邻堆块（曾触发 uploader 栈 canary + 堆 free-list 损坏）；
 * 预留充足余量 */
#define UPLOADER_STACK_SIZE 20480
/* 预分配的上传缓冲：任务启动时一次性从 PSRAM 分配并全程复用，
 * 避免每次任务 malloc/free 大块内存造成的堆碎片化 */
#define UPLOAD_BUF_SIZE (256 * 1024)

static TaskHandle_t s_uploader_task_handle = NULL;
static StaticTask_t s_uploader_task_tcb;
static StackType_t *s_uploader_task_stack = NULL;
static uint8_t *s_upload_buf = NULL;
static uint32_t s_upload_buf_size = 0;

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
      ESP_LOGW(TAG,
               "Job %s reached max retries (%d). Holding for network recovery.",
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

    /* 注意：不要用 get_mdns_server_ip() 把 S3 主机改写为 IP 直连——
     * 该缓存解析的是 CONFIG_SERVER_HOSTNAME（后端 :3000）的接口 IP，
     * minio (:9000) 可能监听在同一主机的另一个网卡 IP 上，
     * 直连缓存 IP 会 Connection reset（此前已踩坑）。
     * 保持主机名由 lwIP/DNS 解析（原上传路径验证可用）。 */

    // 2. 读取二进制图片文件（复用任务启动时预分配的 PSRAM 缓冲）
    FILE *f = fopen(job.path, "rb");
    if (!f) {
      ESP_LOGE(TAG, "File missing: %s", job.path);
      img_queue_commit();
      continue;
    }

    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || (uint32_t)size > s_upload_buf_size) {
      ESP_LOGE(TAG, "File size %d exceeds upload buffer %u, drop job", size,
               s_upload_buf_size);
      fclose(f);
      img_queue_commit();
      continue;
    }

    size_t read_bytes = fread(s_upload_buf, 1, size, f);
    fclose(f);
    if (read_bytes != (size_t)size) {
      ESP_LOGE(TAG, "File read incomplete: %d/%d", (int)read_bytes, size);
      goto fail_retry;
    }

    // 3. PUT 上传 S3（原始预签名 URL，主机名由 esp_http_client/DNS 解析）
    bool put_success = http_put_binary(raw_upload_url, s_upload_buf, size);
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
    int backoff =
        (job.retry_count <= 3) ? RETRY_BACKOFF_MS : RETRY_BACKOFF_LONG_MS;
    ESP_LOGW(TAG, "Job %s failed, retry %d/%d (backoff %d ms)", job.path,
             job.retry_count, MAX_RETRY_LIMIT, backoff);
    vTaskDelay(pdMS_TO_TICKS(backoff));
  }
}

void uploader_task_start(void) {
  if (s_uploader_task_handle != NULL)
    return;

  /* 提前堆分配：上传缓冲 + 任务栈，均在任务创建前一次性分配 */
  s_upload_buf =
      heap_caps_malloc(UPLOAD_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_upload_buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM upload buffer (%d bytes)",
             UPLOAD_BUF_SIZE);
    return;
  }
  s_upload_buf_size = UPLOAD_BUF_SIZE;

  s_uploader_task_stack = (StackType_t *)heap_caps_malloc(
      UPLOADER_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_uploader_task_stack == NULL) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM stack for uploader task");
    heap_caps_free(s_upload_buf);
    s_upload_buf = NULL;
    s_upload_buf_size = 0;
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
    heap_caps_free(s_upload_buf);
    s_upload_buf = NULL;
    s_upload_buf_size = 0;
    return;
  }

  ESP_LOGI(TAG, "Uploader task created with PSRAM stack, size %d bytes",
           UPLOADER_STACK_SIZE);
  TEST_MEM_INFO(TAG);
}