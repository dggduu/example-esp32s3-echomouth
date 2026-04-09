/* main_page.c */
#include "gs_nav.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
  lv_obj_t *root;
  lv_obj_t *lbl_countdown;
  lv_obj_t *lbl_date;
  lv_obj_t *lbl_clock;
  lv_obj_t *lbl_wday;
  uint32_t last_tick;
} main_page_ctx_t;

// 建议在外部或全局初始化一次样式，此处为了演示写在内部
static void setup_styles(main_page_ctx_t *ctx) {
  // 容器样式：去除边框和圆角，减少渲染开销
  lv_obj_set_style_pad_all(ctx->root, 0, 0);
  lv_obj_set_style_border_width(ctx->root, 0, 0);
  lv_obj_set_style_bg_opa(ctx->root, LV_OPA_0, 0);

  // 设置垂直分布，居中对齐
  lv_obj_set_flex_flow(ctx->root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(ctx->root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(ctx->root, 10, 0); // 设置元素间的间距
}

static const char *week_day_map[] = {"SUN", "MON", "TUE", "WED",
                                     "THU", "FRI", "SAT"};

static void main_page_update(void *ctx_in) {
  main_page_ctx_t *ctx = (main_page_ctx_t *)ctx_in;
  if (!ctx || lv_tick_elaps(ctx->last_tick) < 1000)
    return;
  ctx->last_tick = lv_tick_get();

  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  int wday = timeinfo.tm_wday;

  // 仅在必要时更新，或使用单一格式化输出减少底层重绘次数
  lv_label_set_text_fmt(ctx->lbl_clock, "%02d:%02d:%02d", timeinfo.tm_hour,
                        timeinfo.tm_min, timeinfo.tm_sec);
  lv_label_set_text_fmt(ctx->lbl_date, "%d-%02d-%02d", timeinfo.tm_year + 1900,
                        timeinfo.tm_mon + 1, timeinfo.tm_mday);
  lv_label_set_text_fmt(ctx->lbl_wday, "%s", week_day_map[wday]);
}

static void main_page_date_click_cb(lv_event_t *e) {
  extern const gs_page_desc_t page_menu;
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    gs_nav_push(&page_menu, NULL);
  }
}

static void *main_page_init(void *args) {
  main_page_ctx_t *ctx = (main_page_ctx_t *)malloc(sizeof(main_page_ctx_t));
  if (ctx) {
    memset(ctx, 0, sizeof(main_page_ctx_t));
    ctx->last_tick = lv_tick_get();
  }
  return ctx;
}

static lv_obj_t *main_page_render(lv_obj_t *parent, void *ctx_in) {
  main_page_ctx_t *ctx = (main_page_ctx_t *)ctx_in;

  ctx->root = lv_obj_create(parent);
  lv_obj_set_size(ctx->root, 320, 240);
  setup_styles(ctx); // 注入布局样式

  lv_obj_add_event_cb(ctx->root, main_page_date_click_cb, LV_EVENT_CLICKED,
                      NULL);

  // // 1. 顶部：倒计时 (字号 14)
  // ctx->lbl_countdown = lv_label_create(ctx->root);
  // lv_obj_set_style_text_font(ctx->lbl_countdown, &lv_font_montserrat_14,
  //                            0); // 需确保已启用该字体
  // 2. 中间：时钟 (字号 32 - 核心视觉)
  ctx->lbl_clock = lv_label_create(ctx->root);
  lv_obj_set_style_text_font(ctx->lbl_clock, &lv_font_montserrat_32, 0);
  lv_label_set_text(ctx->lbl_clock, "00:00:00");

  // 3. 底部：日期 (字号 16)
  ctx->lbl_date = lv_label_create(ctx->root);
  lv_obj_set_style_text_font(ctx->lbl_date, &lv_font_montserrat_14, 0);
  lv_obj_add_flag(ctx->lbl_date, LV_OBJ_FLAG_CLICKABLE);
  // lv_obj_add_event_cb(ctx->lbl_date, main_page_date_click_cb,
  // LV_EVENT_CLICKED,
  //                     NULL); // 修复绑定错误
  lv_label_set_text(ctx->lbl_date, "2026-04-07");

  // 4. 辅助信息 (字号 12 - 比如星期)
  ctx->lbl_wday = lv_label_create(ctx->root);
  lv_obj_set_style_text_font(ctx->lbl_wday, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(ctx->lbl_wday, lv_palette_main(LV_PALETTE_GREY),
                              0);
  lv_label_set_text(ctx->lbl_wday, "WAIT");

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