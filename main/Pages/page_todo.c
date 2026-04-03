#include "cJSON.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "gs_nav.h"
#include "http_client_helper.h"
#include "lvgl.h"
#include "nvs_helper.h"
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
  int32_t deviceId;

  task_item_t tasks[MAX_TASKS];

  lv_obj_t *main_cont; // 修改为 main 容器
  lv_obj_t *btn_prev;  // 新增上一页按钮
  lv_obj_t *btn_next;
  lv_obj_t *lbl_page; // 新增页码显示
} page_todo_ctx_t;

static page_todo_ctx_t s_ctx;

/* ================= 辅助函数 ================= */

// 根据状态返回 Element Plus 风格的浅色背景色
static lv_color_t get_status_color(const char *status) {
  if (strcmp(status, "active") == 0)
    return lv_color_hex(0xECF5FF);
  if (strcmp(status, "completed") == 0)
    return lv_color_hex(0xF0F9EB);
  if (strcmp(status, "pending_review") == 0)
    return lv_color_hex(0xFDF6EC);
  if (strcmp(status, "rejected") == 0)
    return lv_color_hex(0xFEF0F0);
  return lv_color_hex(0xF4F4F5); // pending
}

// 根据状态返回对应的文字颜色
static lv_color_t get_status_text_color(const char *status) {
  if (strcmp(status, "active") == 0)
    return lv_color_hex(0x409EFF);
  if (strcmp(status, "completed") == 0)
    return lv_color_hex(0x67C23A);
  if (strcmp(status, "pending_review") == 0)
    return lv_color_hex(0xE6A23C);
  if (strcmp(status, "rejected") == 0)
    return lv_color_hex(0xF56C6C);
  return lv_color_hex(0x909399); // pending
}

/* ================= HTTP ================= */

static bool fetch_tasks(page_todo_ctx_t *ctx) {
  char path[128];
  // 使用 NVS 中取出的 userId 替代硬编码
  snprintf(path, sizeof(path), "/device/%ld/tasks?page=%d&limit=3",
           ctx->deviceId, ctx->page);

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

  ctx->has_more = (count == MAX_TASKS); // 简单的分页判断逻辑补充

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
  snprintf(path, sizeof(path), "/device/%d/start", task_id);
  time_t now = time(NULL);

  // 彻底解决 %lld 格式化陷阱，使用 cJSON 序列化
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "deviceId", s_ctx.deviceId); // 修复硬编码
  cJSON_AddNumberToObject(root, "startTime", (double)now);

  char *body = cJSON_PrintUnformatted(root);
  if (body) {
    if (!http_post_json(path, body)) {
      ESP_LOGE(TAG, "start_task failed for id=%d", task_id);
    }
    free(body);
  }
  cJSON_Delete(root);
}

static void complete_task(int task_id) {
  char path[64];
  snprintf(path, sizeof(path), "/device/%d/complete", task_id);
  time_t now = time(NULL);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "deviceId", s_ctx.deviceId);
  cJSON_AddNumberToObject(root, "endTime", (double)now);
  cJSON_AddStringToObject(root, "imagePath", "");
  cJSON_AddStringToObject(root, "type", "timed");

  char *body = cJSON_PrintUnformatted(root);
  if (body) {
    if (!http_post_json(path, body)) {
      ESP_LOGE(TAG, "complete_task failed for id=%d", task_id);
    }
    free(body);
  }
  cJSON_Delete(root);
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

static void update_pagination_ui(page_todo_ctx_t *ctx) {
  lv_label_set_text_fmt(ctx->lbl_page, "Page\n%d", ctx->page + 1);

  if (ctx->page > 0)
    lv_obj_clear_flag(ctx->btn_prev, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(ctx->btn_prev, LV_OBJ_FLAG_HIDDEN);

  if (ctx->has_more)
    lv_obj_clear_flag(ctx->btn_next, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(ctx->btn_next, LV_OBJ_FLAG_HIDDEN);
}

static void render_list(page_todo_ctx_t *ctx) {
  lv_obj_clean(ctx->main_cont);

  for (int i = 0; i < ctx->task_count; i++) {
    // 创建卡片容器
    lv_obj_t *card = lv_obj_create(ctx->main_cont);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    // 应用 Element Plus 状态色彩
    lv_obj_set_style_bg_color(card, get_status_color(ctx->tasks[i].status), 0);
    lv_obj_set_style_border_width(card, 0, 0); // 去除默认边框，更现代
    lv_obj_set_style_radius(card, 8, 0);       // 圆角

    // 标题与状态行 (Flex Row)
    lv_obj_t *header_row = lv_obj_create(card);
    lv_obj_set_size(header_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(header_row, 0, 0);
    lv_obj_set_style_bg_opa(header_row, 0, 0);
    lv_obj_set_style_border_width(header_row, 0, 0);

    lv_obj_t *title = lv_label_create(header_row);
    lv_label_set_text(title, ctx->tasks[i].title);
    // lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    lv_obj_t *status = lv_label_create(header_row);
    lv_label_set_text(status, ctx->tasks[i].status);
    lv_obj_set_style_text_color(status,
                                get_status_text_color(ctx->tasks[i].status), 0);

    // 描述文本
    lv_obj_t *desc = lv_label_create(card);
    lv_label_set_text(desc, ctx->tasks[i].desc);
    lv_obj_set_style_text_color(desc, lv_color_hex(0x606266),
                                0); // El-text-regular

    // 操作按钮行
    lv_obj_t *btn_row = lv_obj_create(card);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_bg_opa(btn_row, 0, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);

    // 仅在合法状态下显示按钮，提升交互逻辑
    if (strcmp(ctx->tasks[i].status, "pending") == 0) {
      lv_obj_t *btn_start = lv_btn_create(btn_row);
      lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x409EFF), 0);
      lv_label_set_text(lv_label_create(btn_start), "Start");
      lv_obj_add_event_cb(btn_start, on_start_click, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
    } else if (strcmp(ctx->tasks[i].status, "active") == 0) {
      lv_obj_t *btn_complete = lv_btn_create(btn_row);
      lv_obj_set_style_bg_color(btn_complete, lv_color_hex(0x67C23A), 0);
      lv_label_set_text(lv_label_create(btn_complete), "Complete");
      lv_obj_add_event_cb(btn_complete, on_complete_click, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
    }
  }

  update_pagination_ui(ctx);
  lv_obj_invalidate(ctx->main_cont);
}

/* ================= 按钮与导航 ================= */

static void load_and_render() {
  if (fetch_tasks(&s_ctx)) {
    if (lvgl_port_lock(0)) {
      render_list(&s_ctx);
      lvgl_port_unlock();
    }
  }
}

static void btn_next_event(lv_event_t *e) {
  s_ctx.page++;
  load_and_render();
}

static void btn_prev_event(lv_event_t *e) {
  if (s_ctx.page > 0) {
    s_ctx.page--;
    load_and_render();
  }
}

static void btn_back_event(lv_event_t *e) { gs_nav_pop(); }

/* ================= 生命周期 ================= */

static void *page_todo_init(void *args) {
  memset(&s_ctx, 0, sizeof(s_ctx));
  s_ctx.page = 0;
  s_ctx.has_more = true;

  // 工业级警告：这里的前提是 app_main 已调用 nvs_flash_init()
  if (nvs_helper_get_i32("storage", "device_id", &s_ctx.deviceId) != ESP_OK) {
    s_ctx.deviceId = 2; // 降级处理：如果没有取到，提供默认值防止奔溃
    ESP_LOGW(TAG, "Failed to get parent_id, defaulting to 1");
  }
  return &s_ctx;
}

static void page_todo_deinit(void *ctx) {}

/* ================= 页面渲染 (Left-Main 架构) ================= */

static lv_obj_t *page_todo_render(lv_obj_t *parent, void *ctx_ptr) {
  page_todo_ctx_t *ctx = ctx_ptr;

  // 根容器：Flex Row
  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(root, 0, 0);

  // ================= Left Panel (导航与控制) =================
  lv_obj_t *left_panel = lv_obj_create(root);
  lv_obj_set_size(left_panel, 80, LV_PCT(100)); // 固定宽度 80px
  lv_obj_set_flex_flow(left_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(left_panel, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(left_panel, lv_color_hex(0x2C3E50),
                            0); // 深色左侧边栏
  lv_obj_set_style_border_width(left_panel, 0, 0);
  lv_obj_set_style_radius(left_panel, 0, 0);

  lv_obj_t *btn_back = lv_btn_create(left_panel);
  lv_obj_set_width(btn_back, LV_PCT(80));
  lv_label_set_text(lv_label_create(btn_back), "< Back");
  lv_obj_add_event_cb(btn_back, btn_back_event, LV_EVENT_CLICKED, NULL);

  // 中间放页码和翻页按钮
  lv_obj_t *page_ctrl_cont = lv_obj_create(left_panel);
  lv_obj_set_width(page_ctrl_cont, LV_PCT(100));
  lv_obj_set_flex_flow(page_ctrl_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page_ctrl_cont, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(page_ctrl_cont, 0, 0);
  lv_obj_set_style_border_width(page_ctrl_cont, 0, 0);

  ctx->btn_prev = lv_btn_create(page_ctrl_cont);
  lv_obj_set_width(ctx->btn_prev, LV_PCT(80));
  lv_label_set_text(lv_label_create(ctx->btn_prev), "Prev");
  lv_obj_add_event_cb(ctx->btn_prev, btn_prev_event, LV_EVENT_CLICKED, NULL);

  ctx->lbl_page = lv_label_create(page_ctrl_cont);
  lv_obj_set_style_text_color(ctx->lbl_page, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(ctx->lbl_page, LV_TEXT_ALIGN_CENTER, 0);

  ctx->btn_next = lv_btn_create(page_ctrl_cont);
  lv_obj_set_width(ctx->btn_next, LV_PCT(80));
  lv_label_set_text(lv_label_create(ctx->btn_next), "Next");
  lv_obj_add_event_cb(ctx->btn_next, btn_next_event, LV_EVENT_CLICKED, NULL);

  // ================= Main Panel (内容列表) =================
  ctx->main_cont = lv_obj_create(root);
  lv_obj_set_height(ctx->main_cont, LV_PCT(100));
  lv_obj_set_flex_grow(ctx->main_cont, 1); // 占据剩余全部宽度
  lv_obj_set_flex_flow(ctx->main_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_color(ctx->main_cont, lv_color_hex(0xF0F2F5),
                            0); // 浅灰背景
  lv_obj_set_style_border_width(ctx->main_cont, 0, 0);
  lv_obj_set_style_radius(ctx->main_cont, 0, 0);

  load_and_render(); // 初始拉取数据并渲染

  return root;
}

const gs_page_desc_t page_todo = {
    .init_cb = page_todo_init,
    .render_cb = page_todo_render,
    .update_cb = NULL,
    .deinit_cb = page_todo_deinit,
};