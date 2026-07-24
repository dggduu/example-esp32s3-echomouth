#include "StyleSheet.h"
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
    lv_label_set_text_fmt(ctx->lbl_task_name, "当前任务: %s", task_title);
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

static void main_page_click_cb(lv_event_t *e) {
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
  lv_obj_set_flex_align(ctx->root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(ctx->root, 28, 0);
  lv_obj_set_style_pad_gap(ctx->root, 16, 0);
  lv_obj_set_style_bg_opa(ctx->root, LV_OPA_TRANSP, 0);

  /* 给根容器添加可点击标志并绑定跳转事件 */
  lv_obj_add_flag(ctx->root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(ctx->root, main_page_click_cb, LV_EVENT_CLICKED, NULL);

  /* clock area */
  lv_obj_t *ca = lv_obj_create(ctx->root);
  lv_obj_set_size(ca, LV_PCT(80), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(ca, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(ca, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(ca, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ca, 0, 0);
  lv_obj_set_style_pad_gap(ca, 4, 0);

  ctx->lbl_clock = lv_label_create(ca);
  lv_obj_set_style_text_font(ctx->lbl_clock, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(ctx->lbl_clock, S_COLOR_ON_BACKGROUND, 0);
  lv_label_set_text(ctx->lbl_clock, "00:00:00");

  ctx->lbl_wday = lv_label_create(ca);
  lv_obj_set_style_text_color(ctx->lbl_wday, S_TEXT_SECONDARY, 0);
  lv_label_set_text(ctx->lbl_wday, "周一");

  ctx->lbl_date = lv_label_create(ca);
  lv_obj_set_style_text_font(ctx->lbl_date, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(ctx->lbl_date, S_COLOR_PRIMARY, 0);
  lv_label_set_text(ctx->lbl_date, "0000-00-00");

  /* task card */
  lv_obj_t *tc = lv_obj_create(ctx->root);
  lv_obj_set_size(tc, LV_PCT(80), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(tc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_color(tc, S_BG_CARD, 0);
  lv_obj_set_style_radius(tc, S_RADIUS_CARD, 0);
  lv_obj_set_style_border_width(tc, 0, 0);
  lv_obj_set_style_pad_all(tc, S_PAD_H, 0);
  lv_obj_set_style_pad_gap(tc, 4, 0);

  ctx->lbl_task_name = lv_label_create(tc);
  lv_label_set_long_mode(ctx->lbl_task_name, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(ctx->lbl_task_name, LV_PCT(100));
  lv_obj_set_style_text_color(ctx->lbl_task_name, S_TEXT_PRIMARY, 0);
  lv_label_set_text(ctx->lbl_task_name, "暂无活跃任务");

  ctx->lbl_task_duration = lv_label_create(tc);
  lv_obj_set_style_text_color(ctx->lbl_task_duration, S_TEXT_SECONDARY, 0);
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