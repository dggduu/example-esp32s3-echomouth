#include "StyleSheet.h"
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

LV_FONT_DECLARE(chili_cn);

static lv_color_t get_status_color(const char *status) {
  if (strcmp(status, "active") == 0)
    return S_COLOR_PRIMARY_CONTAINER;
  if (strcmp(status, "completed") == 0)
    return lv_color_hex(0xE8F5E9);
  if (strcmp(status, "pending_review") == 0)
    return lv_color_hex(0xFFF3E0);
  if (strcmp(status, "rejected") == 0)
    return S_COLOR_ERROR_CONTAINER;
  if (strcmp(status, "pending") == 0)
    return S_COLOR_SURFACE_LOW;
  return S_COLOR_SURFACE_LOW;
}

static lv_color_t get_status_text_color(const char *status) {
  if (strcmp(status, "active") == 0)
    return S_COLOR_ON_PRIMARY_CONTAINER;
  if (strcmp(status, "completed") == 0)
    return lv_color_hex(0x2E7D32);
  if (strcmp(status, "pending_review") == 0)
    return lv_color_hex(0xE65100);
  if (strcmp(status, "rejected") == 0)
    return S_COLOR_ERROR;
  if (strcmp(status, "pending") == 0)
    return S_TEXT_SECONDARY;
  return S_TEXT_SECONDARY;
}

static lv_color_t get_status_btn_color(const char *status) {
  if (strcmp(status, "active") == 0)
    return lv_color_hex(0x4CAF50);
  if (strcmp(status, "rejected") == 0)
    return S_COLOR_ERROR;
  return S_COLOR_PRIMARY;
}

static void format_deadline(int64_t ms, char *buffer, size_t buf_size) {
  if (ms <= 0) {
    snprintf(buffer, buf_size, "No deadline");
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
    gs_toast_show("Task started", GS_TOAST_SUCCESS);
    load_and_render();
  } else {
    gs_toast_show("Complete current task first", GS_TOAST_FAILED);
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
    gs_toast_show("Out of memory", GS_TOAST_FAILED);
  }
}

static void update_pagination_ui(page_todo_ctx_t *ctx) {
  lv_label_set_text_fmt(ctx->lbl_page, "%d", ctx->page + 1);
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
    lv_obj_set_style_radius(card, S_RADIUS_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 12, 0);

    /* header: title */
    lv_obj_t *lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, item->title);
    lv_obj_set_style_text_font(lbl_title, &chili_cn, 0);
    lv_obj_set_style_text_color(lbl_title, S_TEXT_PRIMARY, 0);

    /* info line */
    char deadline_str[32];
    format_deadline(item->deadline, deadline_str, sizeof(deadline_str));
    lv_obj_t *lbl_deadline = lv_label_create(card);
    lv_label_set_text_fmt(lbl_deadline, "Deadline: %s", deadline_str);
    lv_obj_set_style_text_color(lbl_deadline, S_TEXT_SECONDARY, 0);
    lv_obj_set_style_margin_top(lbl_deadline, 4, 0);

    /* action button */
    if (strcmp(item->status, "pending") == 0 ||
        strcmp(item->status, "active") == 0 ||
        strcmp(item->status, "rejected") == 0) {

      lv_obj_t *btn = lv_btn_create(card);
      lv_obj_set_style_radius(btn, S_RADIUS_BTN, 0);
      lv_obj_set_style_bg_color(btn, get_status_btn_color(item->status), 0);
      lv_obj_set_style_border_width(btn, 0, 0);
      lv_obj_set_style_pad_hor(btn, 20, 0);
      lv_obj_set_style_pad_ver(btn, 6, 0);
      lv_obj_set_style_margin_top(btn, 8, 0);

      lv_obj_t *btn_lbl = lv_label_create(btn);
      lv_obj_set_style_text_color(btn_lbl, S_TEXT_ON_DARK, 0);

      if (strcmp(item->status, "pending") == 0) {
        lv_label_set_text(btn_lbl, "Start");
        lv_obj_add_event_cb(btn, on_start_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
      } else if (strcmp(item->status, "active") == 0) {
        lv_label_set_text(btn_lbl, "Submit");
        lv_obj_add_event_cb(btn, on_complete_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
      } else if (strcmp(item->status, "rejected") == 0) {
        lv_label_set_text(btn_lbl, "Resubmit");
        lv_obj_add_event_cb(btn, on_complete_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
      }
    } else {
      /* read-only status badge */
      lv_obj_t *badge = lv_label_create(card);
      const char *txt =
          strcmp(item->status, "completed") == 0 ? "Done" : "Reviewing";
      lv_label_set_text(badge, txt);
      lv_obj_set_style_text_color(badge, get_status_text_color(item->status),
                                  0);
      lv_obj_set_style_margin_top(badge, 8, 0);
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
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);

  /* sidebar */
  lv_obj_t *side = lv_obj_create(root);
  lv_obj_set_size(side, 56, LV_PCT(100));
  lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(side, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(side, S_COLOR_SURFACE_MID, 0);
  lv_obj_set_style_radius(side, 0, 0);
  lv_obj_set_style_border_width(side, 0, 0);
  lv_obj_set_style_pad_ver(side, S_PAD_H, 0);

  lv_obj_t *back = lv_btn_create(side);
  lv_obj_set_style_radius(back, S_RADIUS_BTN, 0);
  lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(back, 0, 0);
  lv_obj_t *back_label = lv_label_create(back);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(back_label, S_TEXT_PRIMARY, 0);
  lv_obj_add_event_cb(back, btn_back_event, LV_EVENT_CLICKED, NULL);

  ctx->btn_prev = lv_btn_create(side);
  lv_obj_set_style_radius(ctx->btn_prev, S_RADIUS_BTN, 0);
  lv_obj_set_style_bg_opa(ctx->btn_prev, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ctx->btn_prev, 0, 0);
  lv_obj_t *prev_label = lv_label_create(ctx->btn_prev);
  lv_label_set_text(prev_label, LV_SYMBOL_PREV);
  lv_obj_set_style_text_color(prev_label, S_TEXT_PRIMARY, 0);
  lv_obj_add_event_cb(ctx->btn_prev, btn_prev_event, LV_EVENT_CLICKED, NULL);

  ctx->lbl_page = lv_label_create(side);
  lv_obj_set_style_text_color(ctx->lbl_page, S_TEXT_PRIMARY, 0);
  lv_obj_set_style_text_align(ctx->lbl_page, LV_TEXT_ALIGN_CENTER, 0);

  ctx->btn_next = lv_btn_create(side);
  lv_obj_set_style_radius(ctx->btn_next, S_RADIUS_BTN, 0);
  lv_obj_set_style_bg_opa(ctx->btn_next, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ctx->btn_next, 0, 0);
  lv_obj_t *next_label = lv_label_create(ctx->btn_next);
  lv_label_set_text(next_label, LV_SYMBOL_NEXT);
  lv_obj_set_style_text_color(next_label, S_TEXT_PRIMARY, 0);
  lv_obj_add_event_cb(ctx->btn_next, btn_next_event, LV_EVENT_CLICKED, NULL);

  /* content */
  ctx->main_cont = lv_obj_create(root);
  lv_obj_set_height(ctx->main_cont, LV_PCT(100));
  lv_obj_set_flex_grow(ctx->main_cont, 1);
  lv_obj_set_flex_flow(ctx->main_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(ctx->main_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(ctx->main_cont, S_PAD_H, 0);
  lv_obj_set_style_border_width(ctx->main_cont, 0, 0);
  lv_obj_set_style_pad_gap(ctx->main_cont, S_GAP, 0);

  load_and_render();
  return root;
}

const gs_page_desc_t page_todo = {
    .init_cb = page_todo_init,
    .render_cb = page_todo_render,
    .update_cb = NULL,
    .deinit_cb = page_todo_deinit,
};
