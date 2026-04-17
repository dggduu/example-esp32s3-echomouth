
#include "gs_nav.h"
#include "task_manager.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
  lv_obj_t *root;
  lv_obj_t *lbl_clock;
  lv_obj_t *lbl_date;
  lv_obj_t *lbl_wday;
  lv_obj_t *lbl_task_name;
  lv_obj_t *lbl_task_duration;
  uint32_t last_tick;
} main_page_ctx_t;

static const char *week_day_map[] = {"周日", "周一", "周二", "周三",
                                     "周四", "周五", "周六"};

static void main_page_update(void *ctx_in) {
  main_page_ctx_t *ctx = (main_page_ctx_t *)ctx_in;
  if (!ctx || lv_tick_elaps(ctx->last_tick) < 1000)
    return;
  ctx->last_tick = lv_tick_get();

  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  lv_label_set_text_fmt(ctx->lbl_clock, "%02d:%02d:%02d", timeinfo.tm_hour,
                        timeinfo.tm_min, timeinfo.tm_sec);
  lv_label_set_text_fmt(ctx->lbl_date, "%d-%02d-%02d", timeinfo.tm_year + 1900,
                        timeinfo.tm_mon + 1, timeinfo.tm_mday);
  lv_label_set_text_fmt(ctx->lbl_wday, "%s", week_day_map[timeinfo.tm_wday]);

  const char *task_title = task_manager_get_active_title();
  if (task_title && strlen(task_title) > 0) {
    lv_label_set_text_fmt(ctx->lbl_task_name, "当前任务:%s", task_title);
  } else {
    lv_label_set_text(ctx->lbl_task_name, "暂无活跃任务");
  }

  time_t start_time = task_manager_get_start_time();
  if (start_time > 0 && now > start_time) {
    int diff = (int)(now - start_time);
    int hours = diff / 3600;
    int minutes = (diff % 3600) / 60;
    int seconds = diff % 60;
    lv_label_set_text_fmt(ctx->lbl_task_duration, "已进行 %02d:%02d:%02d",
                          hours, minutes, seconds);
  } else {
    lv_label_set_text(ctx->lbl_task_duration, "");
  }
}

static void main_page_date_click_cb(lv_event_t *e) {
  extern const gs_page_desc_t page_menu;
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    gs_nav_push(&page_menu, NULL);
  }
}

static void *main_page_init(void *args) {
  main_page_ctx_t *ctx = malloc(sizeof(main_page_ctx_t));
  if (ctx) {
    memset(ctx, 0, sizeof(main_page_ctx_t));
    ctx->last_tick = lv_tick_get();
  }
  return ctx;
}

static lv_obj_t *main_page_render(lv_obj_t *parent, void *ctx_in) {
  main_page_ctx_t *ctx = (main_page_ctx_t *)ctx_in;

  ctx->root = lv_obj_create(parent);
  lv_obj_set_size(ctx->root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(ctx->root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(ctx->root, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(ctx->root, 20, 0);
  lv_obj_set_style_bg_color(ctx->root, lv_color_white(), 0);
  lv_obj_set_style_border_width(ctx->root, 0, 0);

  lv_obj_t *top_area = lv_obj_create(ctx->root);
  lv_obj_set_width(top_area, LV_PCT(100));
  lv_obj_set_height(top_area, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(top_area, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(top_area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top_area, 0, 0);
  lv_obj_set_style_pad_all(top_area, 0, 0);
  lv_obj_set_style_pad_gap(top_area, 5, 0);

  ctx->lbl_clock = lv_label_create(top_area);
  lv_obj_set_style_text_font(ctx->lbl_clock, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(ctx->lbl_clock, lv_color_black(), 0);
  lv_label_set_text(ctx->lbl_clock, "00:00:00");

  lv_obj_t *date_wday_cont = lv_obj_create(top_area);
  lv_obj_set_width(date_wday_cont, LV_PCT(100));
  lv_obj_set_height(date_wday_cont, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(date_wday_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(date_wday_cont, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(date_wday_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(date_wday_cont, 0, 0);
  lv_obj_set_style_pad_all(date_wday_cont, 0, 0);
  lv_obj_set_style_pad_gap(date_wday_cont, 10, 0);

  ctx->lbl_date = lv_label_create(date_wday_cont);
  lv_obj_set_style_text_font(ctx->lbl_date, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(ctx->lbl_date, lv_color_hex(0x333333), 0);
  lv_obj_add_flag(ctx->lbl_date, LV_OBJ_FLAG_CLICKABLE);
  lv_label_set_text(ctx->lbl_date, "0000-00-00");
  lv_obj_add_event_cb(ctx->root, main_page_date_click_cb, LV_EVENT_CLICKED,
                      NULL);

  ctx->lbl_wday = lv_label_create(date_wday_cont);
  lv_obj_set_style_text_color(ctx->lbl_wday, lv_color_hex(0x333333), 0);
  lv_label_set_text(ctx->lbl_wday, "周一");

  lv_obj_t *bottom_area = lv_obj_create(ctx->root);
  lv_obj_set_width(bottom_area, LV_PCT(100));
  lv_obj_set_height(bottom_area, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(bottom_area, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(bottom_area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_bg_opa(bottom_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bottom_area, 0, 0);
  lv_obj_set_style_pad_all(bottom_area, 0, 0);
  lv_obj_set_style_pad_gap(bottom_area, 5, 0);

  ctx->lbl_task_name = lv_label_create(bottom_area);
  lv_obj_set_style_text_color(ctx->lbl_task_name, lv_color_hex(0x333333), 0);
  lv_label_set_text(ctx->lbl_task_name, "暂无活跃任务");

  ctx->lbl_task_duration = lv_label_create(bottom_area);
  lv_obj_set_style_text_color(ctx->lbl_task_duration, lv_color_hex(0x666666),
                              0);
  lv_label_set_text(ctx->lbl_task_duration, "");

  main_page_update(ctx);

  return ctx->root;
}

static void main_page_deinit(void *ctx_in) {
  if (ctx_in)
    free(ctx_in);
}

const gs_page_desc_t page_main = {.init_cb = main_page_init,
                                  .render_cb = main_page_render,
                                  .update_cb = main_page_update,
                                  .deinit_cb = main_page_deinit};