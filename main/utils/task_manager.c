#include "task_manager.h"
#include "cJSON.h"
#include "esp_log.h"
#include "http_client_helper.h"
#include "nvs_helper.h"

static const char *TAG = "TASK_MGR";
static const char *NVS_NS = "business";
static const char *KEY_ACTIVE_ID = "active_tid";

#define KEY_ACTIVE_TITLE "active_title"

#define KEY_ACTIVE_START "active_start"

static struct {
  int32_t deviceId;
  int32_t active_task_id;
  char active_title[64];
  int32_t start_time;
} s_mgr;

bool task_manager_init(int32_t device_id) {
  s_mgr.deviceId = device_id;
  if (nvs_helper_get_i32(NVS_NS, KEY_ACTIVE_ID, &s_mgr.active_task_id) !=
      ESP_OK) {
    s_mgr.active_task_id = 0;
  }
  // 修正：传递缓冲区大小，而不是指针
  if (nvs_helper_get_string(NVS_NS, KEY_ACTIVE_TITLE, s_mgr.active_title,
                            sizeof(s_mgr.active_title)) != ESP_OK) {
    s_mgr.active_title[0] = '\0';
  }
  if (nvs_helper_get_i32(NVS_NS, KEY_ACTIVE_START, &s_mgr.start_time) !=
      ESP_OK) {
    s_mgr.start_time = 0;
  }
  ESP_LOGI(TAG, "Init: active_id=%ld, title=%s, start_time=%ld",
           s_mgr.active_task_id, s_mgr.active_title, s_mgr.start_time);
  return true;
}

int32_t task_manager_get_active_id(void) { return s_mgr.active_task_id; }

bool task_manager_fetch_list(page_todo_ctx_t *ctx) {
  char path[128];
  snprintf(path, sizeof(path), "/device/%d/tasks?page=%d&limit=%d",
           (int)s_mgr.deviceId, ctx->page, MAX_TASKS);

  static char json_buf[1024 * 6]; // 服务端返回 monitorConfig 等大字段需要较大缓冲
  if (!http_get_json(path, json_buf, sizeof(json_buf)))
    return false;

  cJSON *root = cJSON_Parse(json_buf);
  if (!root)
    return false;

  cJSON *tasks = cJSON_GetObjectItem(root, "tasks");
  if (!cJSON_IsArray(tasks)) {
    cJSON_Delete(root);
    return false;
  }

  int count = cJSON_GetArraySize(tasks);
  ctx->task_count = 0;
  ctx->has_more = (count >= MAX_TASKS);

  for (int i = 0; i < count && i < MAX_TASKS; i++) {
    cJSON *item = cJSON_GetArrayItem(tasks, i);
    task_item_t *t = &ctx->tasks[i];

    t->id = cJSON_GetObjectItem(item, "id")->valueint;
    // 使用 strncpy 防止溢出
    const char *title = cJSON_GetObjectItem(item, "title")->valuestring;
    const char *desc = cJSON_GetObjectItem(item, "desc")->valuestring;
    const char *status = cJSON_GetObjectItem(item, "status")->valuestring;

    snprintf(t->title, sizeof(t->title), "%s", title ? title : "");
    snprintf(t->desc, sizeof(t->desc), "%s", desc ? desc : "");
    snprintf(t->status, sizeof(t->status), "%s", status ? status : "");

    cJSON *deadline_obj = cJSON_GetObjectItem(item, "deadline");
    if (cJSON_IsNumber(deadline_obj)) {
      t->deadline = (int64_t)deadline_obj->valuedouble;
    } else {
      t->deadline = 0;
    }

    cJSON *likes_obj = cJSON_GetObjectItem(item, "likes");
    if (cJSON_IsNumber(likes_obj)) {
      t->likes = likes_obj->valueint;
    } else {
      t->likes = 0;
    }

    ctx->task_count++;
  }

  cJSON_Delete(root);
  return true;
}

bool task_manager_start(int task_id, const char *title) {
  if (s_mgr.active_task_id != 0) {
    ESP_LOGW(TAG, "Task %ld already running. Cannot start %d",
             s_mgr.active_task_id, task_id);
    return false;
  }

  char path[64];
  snprintf(path, sizeof(path), "/device/%d/start", task_id);
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "deviceId", s_mgr.deviceId);
  cJSON_AddNumberToObject(root, "startTime", (double)time(NULL));
  char *body = cJSON_PrintUnformatted(root);

  bool success = false;
  if (body) {
    if (http_post_json(path, body)) {
      s_mgr.active_task_id = task_id;
      nvs_helper_set_i32(NVS_NS, KEY_ACTIVE_ID, task_id);

      if (title && strlen(title) > 0) {
        strlcpy(s_mgr.active_title, title, sizeof(s_mgr.active_title));
        nvs_helper_set_string(NVS_NS, KEY_ACTIVE_TITLE, s_mgr.active_title);
      } else {
        s_mgr.active_title[0] = '\0';
        nvs_helper_erase_key(NVS_NS, KEY_ACTIVE_TITLE);
      }

      // 记录开始时间（int32_t 存储）
      time_t now = time(NULL);
      s_mgr.start_time = (int32_t)now;
      nvs_helper_set_i32(NVS_NS, KEY_ACTIVE_START, s_mgr.start_time);

      ESP_LOGI(TAG, "Task %d started. Title=%s, start_time=%ld", task_id,
               s_mgr.active_title, s_mgr.start_time);
      success = true;
    }
    free(body);
  }
  cJSON_Delete(root);
  return success;
}

bool task_manager_complete(int task_id) {
  if (s_mgr.active_task_id != task_id) {
    ESP_LOGE(TAG, "ID mismatch: current=%ld, target=%d", s_mgr.active_task_id,
             task_id);
    return false;
  }

  char path[64];
  snprintf(path, sizeof(path), "/device/%d/complete", task_id);
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "deviceId", s_mgr.deviceId);
  cJSON_AddNumberToObject(root, "endTime", (double)time(NULL));
  cJSON_AddStringToObject(root, "imagePath", "");
  cJSON_AddStringToObject(root, "type", "timed");
  char *body = cJSON_PrintUnformatted(root);

  bool success = false;
  if (body) {
    if (http_post_json(path, body)) {
      s_mgr.active_task_id = 0;
      s_mgr.active_title[0] = '\0';
      s_mgr.start_time = 0;
      nvs_helper_erase_key(NVS_NS, KEY_ACTIVE_ID);
      nvs_helper_erase_key(NVS_NS, KEY_ACTIVE_TITLE);
      nvs_helper_erase_key(NVS_NS, KEY_ACTIVE_START);
      ESP_LOGI(TAG, "Task %d completed, state cleared.", task_id);
      success = true;
    }
    free(body);
  }
  cJSON_Delete(root);
  return success;
}

const char *task_manager_get_active_title(void) { return s_mgr.active_title; }

time_t task_manager_get_start_time(void) { return (time_t)s_mgr.start_time; }