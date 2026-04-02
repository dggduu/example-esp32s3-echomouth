#include "s3_helper.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "http_client_helper.h"
#include "img_stack.h"
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include <freertos/task.h>

#define DEVICE_ID 1
#define TASK_ID 1

static void uploader_task(void *arg) {
  char path[128];

  while (1) {
    if (!img_stack_pop(path)) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    /* 1. 获取 upload-url */

    char url[128];
    sprintf(url, "http://localhost:3000/device/%d/upload-url?deviceId=%d",
            DEVICE_ID, DEVICE_ID);

    char resp[512];

    if (!http_get_json(url, resp, sizeof(resp)))
      continue;

    cJSON *root = cJSON_Parse(resp);
    if (!root)
      continue;

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) {
      cJSON_Delete(root);
      continue;
    }

    char *uploadUrl = cJSON_GetObjectItem(data, "uploadUrl")->valuestring;
    char *imageKey = cJSON_GetObjectItem(data, "imageKey")->valuestring;

    /* 2. 读取文件 */

    FILE *f = fopen(path, "rb");
    if (!f) {
      cJSON_Delete(root);
      continue;
    }

    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = malloc(size);
    fread(buf, 1, size, f);
    fclose(f);

    /* 3. PUT 上传 */

    if (!http_put_binary(uploadUrl, buf, size)) {
      free(buf);
      cJSON_Delete(root);
      continue;
    }

    free(buf);

    /* 4. POST /image */

    cJSON *post = cJSON_CreateObject();
    cJSON_AddNumberToObject(post, "deviceId", DEVICE_ID);
    cJSON_AddNumberToObject(post, "taskId", TASK_ID);
    cJSON_AddStringToObject(post, "imageKey", imageKey);
    cJSON_AddNumberToObject(post, "timestamp", esp_timer_get_time() / 1000ULL);

    char *json_str = cJSON_PrintUnformatted(post);

    http_post_json("http://localhost:3000/device/image", json_str);

    cJSON_Delete(post);
    free(json_str);

    cJSON_Delete(root);

    remove(path);
  }
}

void uploader_task_start(void) {
  xTaskCreate(uploader_task, "uploader", 8192, NULL, 5, NULL);
}
