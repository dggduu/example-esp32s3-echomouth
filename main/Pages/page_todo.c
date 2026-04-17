#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "gs_nav.h"
#include "gs_portal.h"
#include "lvgl.h"
#include "monitor_mamager.h"
#include "nvs_helper.h"
#include "task_manager.h"
#include <string.h>
#include <time.h>

#define TAG "PAGE_TODO"

static page_todo_ctx_t s_ctx;

LV_FONT_DECLARE(chinese_font_14px);

static lv_color_t get_status_color(const char *status) {
  if (strcmp(status, "active") == 0)
    return lv_color_hex(0xECF5FF);
  if (strcmp(status, "completed") == 0)
    return lv_color_hex(0xF0F9EB);
  if (strcmp(status, "pending_review") == 0)
    return lv_color_hex(0xFDF6EC);
  if (strcmp(status, "rejected") == 0)
    return lv_color_hex(0xFEF0F0);
  if (strcmp(status, "pending") == 0)
    return lv_color_hex(0xF4F4F5);
  return lv_color_hex(0xF4F4F5);
}

static lv_color_t get_status_text_color(const char *status) {
  if (strcmp(status, "active") == 0)
    return lv_color_hex(0x409EFF);
  if (strcmp(status, "completed") == 0)
    return lv_color_hex(0x67C23A);
  if (strcmp(status, "pending_review") == 0)
    return lv_color_hex(0xE6A23C);
  if (strcmp(status, "rejected") == 0)
    return lv_color_hex(0xF56C6C);
  if (strcmp(status, "pending") == 0)
    return lv_color_hex(0x909399);
  return lv_color_hex(0x909399);
}

static void format_deadline(int64_t ms, char *buffer, size_t buf_size) {
  if (ms <= 0) {
    snprintf(buffer, buf_size, "无截止");
    return;
  }
  time_t seconds = ms / 1000;
  struct tm tm_info;
  localtime_r(&seconds, &tm_info);
  strftime(buffer, buf_size, "%m-%d %H:%M", &tm_info);
}

static void load_and_render(void);

static void on_start_click(lv_event_t *e) {
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  int task_id = s_ctx.tasks[index].id;
  const char *title = s_ctx.tasks[index].title;

  if (task_manager_start(task_id, title)) {
    gs_toast_show("任务开始", GS_TOAST_SUCCESS);
    load_and_render();
  } else {
    gs_toast_show("请先完成当前任务", GS_TOAST_FAILED);
  }
}

extern const gs_page_desc_t page_cam;
#include "cam_shared.h"

static void on_complete_click(lv_event_t *e) {
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  int task_id = s_ctx.tasks[index].id;

  cam_page_args_t *args =
      heap_caps_malloc(sizeof(cam_page_args_t), MALLOC_CAP_INTERNAL);
  if (args) {
    args->task_id = task_id;
    args->device_id = s_ctx.deviceId;
    gs_nav_push(&page_cam, args);
  } else {
    ESP_LOGE(TAG, "Failed to allocate memory for cam page args");
    gs_toast_show("内存不足", GS_TOAST_FAILED);
  }
}

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
    task_item_t *item = &ctx->tasks[i];

    lv_obj_t *card = lv_obj_create(ctx->main_cont);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(card, get_status_color(item->status), 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 10, 0);

    lv_obj_t *header = lv_obj_create(card);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);

    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, item->title);
    lv_obj_set_style_text_font(lbl_title, &chinese_font_14px, 0);

    lv_obj_t *info_line = lv_obj_create(card);
    lv_obj_set_size(info_line, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(info_line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info_line, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(info_line, 0, 0);
    lv_obj_set_style_border_width(info_line, 0, 0);
    lv_obj_set_style_pad_top(info_line, 5, 0);

    char deadline_str[32];
    format_deadline(item->deadline, deadline_str, sizeof(deadline_str));
    lv_obj_t *lbl_deadline = lv_label_create(info_line);
    lv_label_set_text_fmt(lbl_deadline, "截止时间:%s", deadline_str);
    lv_obj_set_style_text_color(lbl_deadline, lv_color_hex(0x909399), 0);

    lv_obj_t *lbl_likes = lv_label_create(info_line);
    if (item->likes > 0) {
      lv_label_set_text(lbl_likes, "已赞");
      lv_obj_set_style_text_color(lbl_likes, lv_color_hex(0xF56C6C), 0);
    } else {

      lv_obj_add_flag(lbl_likes, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *btn_area = lv_obj_create(card);
    lv_obj_set_size(btn_area, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_area, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btn_area, 0, 0);
    lv_obj_set_style_border_width(btn_area, 0, 0);
    lv_obj_set_style_pad_all(btn_area, 0, 0);

    if (strcmp(item->status, "pending") == 0) {
      lv_obj_t *btn = lv_btn_create(btn_area);
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x409EFF), 0);
      lv_label_set_text(lv_label_create(btn), "开始任务");
      lv_obj_add_event_cb(btn, on_start_click, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
    } else if (strcmp(item->status, "active") == 0) {
      lv_obj_t *btn = lv_btn_create(btn_area);
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x67C23A), 0);
      lv_label_set_text(lv_label_create(btn), "提交报告");
      lv_obj_add_event_cb(btn, on_complete_click, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
    } else if (strcmp(item->status, "pending_review") == 0) {
      lv_obj_t *btn = lv_btn_create(btn_area);
      lv_obj_set_style_bg_color(btn, lv_color_hex(0xE6A23C), 0);
      lv_obj_add_state(btn, LV_STATE_DISABLED);
      lv_label_set_text(lv_label_create(btn), "等待审阅");
    } else if (strcmp(item->status, "rejected") == 0) {
      lv_obj_t *btn = lv_btn_create(btn_area);
      lv_obj_set_style_bg_color(btn, lv_color_hex(0xF56C6C), 0);
      lv_label_set_text(lv_label_create(btn), "重新提交");
      lv_obj_add_event_cb(btn, on_complete_click, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
    } else if (strcmp(item->status, "completed") == 0) {
      lv_obj_t *btn = lv_btn_create(btn_area);
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x67C23A), 0);
      lv_label_set_text(lv_label_create(btn), "已完成");
      lv_obj_add_state(btn, LV_STATE_DISABLED);
    }
  }
  update_pagination_ui(ctx);
}

static void load_and_render(void) {
  if (task_manager_fetch_list(&s_ctx)) {
    if (lvgl_port_lock(0)) {
      render_list(&s_ctx);
      lvgl_port_unlock();
    }
  } else {
    gs_portal_toast_show((gs_toast_config_t){.msg = "Failed to fetch tasks",
                                             .type = GS_TOAST_FAILED});
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

static void *page_todo_init(void *args) {
  memset(&s_ctx, 0, sizeof(s_ctx));
  s_ctx.page = 0;
  int32_t dev_id = 1;
  nvs_helper_get_i32("storage", "device_id", &dev_id);
  task_manager_init(dev_id);
  s_ctx.deviceId = dev_id;
  return &s_ctx;
}

static void page_todo_deinit(void *ctx) { (void)ctx; }

static lv_obj_t *page_todo_render(lv_obj_t *parent, void *ctx_ptr) {
  page_todo_ctx_t *ctx = (page_todo_ctx_t *)ctx_ptr;

  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(root, 0, 0);

  lv_obj_t *side = lv_obj_create(root);
  lv_obj_set_size(side, 80, LV_PCT(100));
  lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(side, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(side, lv_color_hex(0x2C3E50), 0);
  lv_obj_set_style_radius(side, 0, 0);
  lv_obj_set_style_border_width(side, 0, 0);

  lv_obj_t *back = lv_btn_create(side);
  lv_obj_t *back_label = lv_label_create(back);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_center(back_label);
  lv_obj_add_event_cb(back, btn_back_event, LV_EVENT_CLICKED, NULL);

  lv_obj_t *ctrls = lv_obj_create(side);
  lv_obj_set_width(ctrls, LV_PCT(100));
  lv_obj_set_flex_flow(ctrls, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(ctrls, 0, 0);
  lv_obj_set_style_border_width(ctrls, 0, 0);

  ctx->btn_prev = lv_btn_create(ctrls);
  lv_obj_t *prev_label = lv_label_create(ctx->btn_prev);
  lv_label_set_text(prev_label, LV_SYMBOL_PREV);
  lv_obj_center(prev_label);
  lv_obj_add_event_cb(ctx->btn_prev, btn_prev_event, LV_EVENT_CLICKED, NULL);

  ctx->lbl_page = lv_label_create(ctrls);
  lv_obj_set_style_text_color(ctx->lbl_page, lv_color_white(), 0);
  lv_obj_set_style_text_align(ctx->lbl_page, LV_TEXT_ALIGN_CENTER, 0);

  ctx->btn_next = lv_btn_create(ctrls);
  lv_obj_t *next_label = lv_label_create(ctx->btn_next);
  lv_label_set_text(next_label, LV_SYMBOL_NEXT);
  lv_obj_center(next_label);
  lv_obj_add_event_cb(ctx->btn_next, btn_next_event, LV_EVENT_CLICKED, NULL);

  ctx->main_cont = lv_obj_create(root);
  lv_obj_set_height(ctx->main_cont, LV_PCT(100));
  lv_obj_set_flex_grow(ctx->main_cont, 1);
  lv_obj_set_flex_flow(ctx->main_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_color(ctx->main_cont, lv_color_hex(0xF0F2F5), 0);
  lv_obj_set_style_pad_all(ctx->main_cont, 10, 0);
  lv_obj_set_style_border_width(ctx->main_cont, 0, 0);

  load_and_render();
  return root;
}

const gs_page_desc_t page_todo = {
    .init_cb = page_todo_init,
    .render_cb = page_todo_render,
    .update_cb = NULL,
    .deinit_cb = page_todo_deinit,
};