#include "cJSON.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "gs_nav.h"
#include "http_client_helper.h"
#include "lvgl.h"
#include "time.h"
#include <stdio.h>
#include <string.h>

#define TAG "PAGE_TODO"

#define MAX_TASKS 3
#define JSON_BUF_SIZE 1024

typedef struct {
  int id;
  char title[32];
  char desc[64];
  char status[16];
} task_item_t;

typedef struct {
  int page;
  bool has_more;
  int task_count;

  task_item_t tasks[MAX_TASKS];

  lv_obj_t *list_cont;
  lv_obj_t *btn_next;
} page_todo_ctx_t;

static page_todo_ctx_t s_ctx;

/* ================= HTTP ================= */

static bool fetch_tasks(page_todo_ctx_t *ctx) {
  char path[128];
  snprintf(path, sizeof(path), "/device/1/tasks?page=%d&limit=3", ctx->page);

  static char json_buf[JSON_BUF_SIZE];

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

  if (count == 0) {
    ctx->has_more = false;
    cJSON_Delete(root);
    return true;
  }

  ctx->has_more = true;

  for (int i = 0; i < count && i < MAX_TASKS; i++) {
    cJSON *item = cJSON_GetArrayItem(tasks, i);
    if (!item)
      continue;

    task_item_t *t = &ctx->tasks[i];

    t->id = cJSON_GetObjectItem(item, "id")->valueint;

    snprintf(t->title, sizeof(t->title), "%s",
             cJSON_GetObjectItem(item, "title")->valuestring);

    snprintf(t->desc, sizeof(t->desc), "%s",
             cJSON_GetObjectItem(item, "desc")->valuestring);

    snprintf(t->status, sizeof(t->status), "%s",
             cJSON_GetObjectItem(item, "status")->valuestring);

    ctx->task_count++;
  }

  cJSON_Delete(root);
  return true;
}

/* ================= API ================= */

static void start_task(int task_id) {
  char path[64];
  // 正确路径: /tasks/123/start
  snprintf(path, sizeof(path), "/tasks/%d/start", task_id);

  time_t now = time(NULL);
  char body[128];
  snprintf(body, sizeof(body), "{\"deviceId\":1,\"startTime\":%lld}", now);

  if (!http_post_json(path, body)) {
    ESP_LOGE(TAG, "start_task failed for id=%d", task_id);
  }
}

static void complete_task(int task_id) {
  char path[64];
  snprintf(path, sizeof(path), "/tasks/%d/complete", task_id);

  time_t now = time(NULL);
  // 注意：imagePath 需从实际上传后获得，此处先使用空字符串
  // type 根据业务选择 "timed" 或 "task_result"
  char body[256];
  snprintf(body, sizeof(body),
           "{"
           "\"deviceId\":1,"
           "\"endTime\":%lld,"
           "\"imagePath\":\"\","
           "\"type\":\"timed\""
           "}",
           now);

  if (!http_post_json(path, body)) {
    ESP_LOGE(TAG, "complete_task failed for id=%d", task_id);
  }
}

/* ================= 事件回调 ================= */

static void on_start_click(lv_event_t *e) {
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  start_task(s_ctx.tasks[index].id);
}

static void on_complete_click(lv_event_t *e) {
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  complete_task(s_ctx.tasks[index].id);
}

/* ================= UI 渲染 ================= */

static void render_list(page_todo_ctx_t *ctx) {
  lv_obj_clean(ctx->list_cont);

  for (int i = 0; i < ctx->task_count; i++) {

    lv_obj_t *item = lv_obj_create(ctx->list_cont);
    lv_obj_set_width(item, LV_PCT(100));
    lv_obj_set_height(item, 70);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title = lv_label_create(item);
    lv_label_set_text_fmt(title, "%s (%s)", ctx->tasks[i].title,
                          ctx->tasks[i].status);

    lv_obj_t *desc = lv_label_create(item);
    lv_label_set_text(desc, ctx->tasks[i].desc);

    lv_obj_t *btn_row = lv_obj_create(item);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, 30);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);

    lv_obj_t *btn_start = lv_btn_create(btn_row);
    lv_obj_set_size(btn_start, 70, 28);
    lv_label_set_text(lv_label_create(btn_start), "Start");

    lv_obj_add_event_cb(btn_start, on_start_click, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);

    lv_obj_t *btn_complete = lv_btn_create(btn_row);
    lv_obj_set_size(btn_complete, 90, 28);
    lv_label_set_text(lv_label_create(btn_complete), "Complete");

    lv_obj_add_event_cb(btn_complete, on_complete_click, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);
  }

  if (ctx->has_more) {
    lv_obj_clear_flag(ctx->btn_next, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(ctx->btn_next, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_invalidate(ctx->list_cont);
}

/* ================= 按钮 ================= */

static void btn_next_event(lv_event_t *e) {
  s_ctx.page++;

  if (fetch_tasks(&s_ctx)) {
    if (lvgl_port_lock(0)) {
      render_list(&s_ctx);
      lvgl_port_unlock();
    }
  }
}

static void btn_back_event(lv_event_t *e) { gs_nav_pop(); }

/* ================= 生命周期 ================= */

static void *page_todo_init(void *args) {
  memset(&s_ctx, 0, sizeof(s_ctx));
  s_ctx.page = 0;
  s_ctx.has_more = true;
  return &s_ctx;
}

static void page_todo_deinit(void *ctx) {}

/* ================= 页面渲染 ================= */

static lv_obj_t *page_todo_render(lv_obj_t *parent, void *ctx_ptr) {
  page_todo_ctx_t *ctx = ctx_ptr;

  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *top = lv_obj_create(root);
  lv_obj_set_width(top, LV_PCT(100));
  lv_obj_set_height(top, 40);

  lv_obj_t *btn_back = lv_btn_create(top);
  lv_obj_set_size(btn_back, 50, 30);
  lv_label_set_text(lv_label_create(btn_back), "<");
  lv_obj_add_event_cb(btn_back, btn_back_event, LV_EVENT_CLICKED, NULL);

  ctx->list_cont = lv_obj_create(root);
  lv_obj_set_width(ctx->list_cont, LV_PCT(100));
  lv_obj_set_flex_grow(ctx->list_cont, 1);
  lv_obj_set_flex_flow(ctx->list_cont, LV_FLEX_FLOW_COLUMN);

  ctx->btn_next = lv_btn_create(root);
  lv_obj_set_size(ctx->btn_next, 80, 35);
  lv_label_set_text(lv_label_create(ctx->btn_next), "Next");
  lv_obj_add_event_cb(ctx->btn_next, btn_next_event, LV_EVENT_CLICKED, NULL);

  if (fetch_tasks(ctx)) {
    render_list(ctx);
  }

  return root;
}

const gs_page_desc_t page_todo = {
    .init_cb = page_todo_init,
    .render_cb = page_todo_render,
    .update_cb = NULL,
    .deinit_cb = page_todo_deinit,
};