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
  uint32_t last_tick; // 用于逻辑上的频率控制
} main_page_ctx_t;

// 页面逻辑更新函数：由外部导航框架的状态机周期性调用
static void main_page_update(void *ctx_in) {
  main_page_ctx_t *ctx = (main_page_ctx_t *)ctx_in;
  if (!ctx)
    return;

  // 频率控制：每 1000ms 执行一次，不阻塞主任务
  if (lv_tick_elaps(ctx->last_tick) < 1000)
    return;
  ctx->last_tick = lv_tick_get();

  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  lv_label_set_text_fmt(ctx->lbl_clock, "%02d:%02d:%02d", timeinfo.tm_hour,
                        timeinfo.tm_min, timeinfo.tm_sec);
  lv_label_set_text_fmt(ctx->lbl_date, "%d/%02d/%02d", timeinfo.tm_year + 1900,
                        timeinfo.tm_mon + 1, timeinfo.tm_mday);
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

  ctx->lbl_countdown = lv_label_create(ctx->root);
  lv_obj_align(ctx->lbl_countdown, LV_ALIGN_CENTER, 0, -40);

  ctx->lbl_date = lv_label_create(ctx->root);
  lv_obj_add_flag(ctx->lbl_date, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(ctx->lbl_date, LV_ALIGN_CENTER, 0, 20);
  lv_obj_add_event_cb(ctx->lbl_date, main_page_date_click_cb, LV_EVENT_CLICKED,
                      NULL);

  ctx->lbl_clock = lv_label_create(ctx->root);
  lv_obj_align(ctx->lbl_clock, LV_ALIGN_CENTER, 0, 50);

  return ctx->root;
}

static void main_page_deinit(void *ctx_in) {
  if (ctx_in)
    free(ctx_in); // 没有任何 timer 需要清理，极大降低 crash 风险
}

const gs_page_desc_t page_main = {
    .init_cb = main_page_init,
    .render_cb = main_page_render,
    .update_cb = main_page_update, // 新增：页面逻辑轮询回调
    .deinit_cb = main_page_deinit};