/* main_page.c */
#include "gs_nav.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 页面私有上下文结构体
typedef struct {
  lv_obj_t *root;
  lv_obj_t *lbl_countdown;
  lv_obj_t *lbl_date;
  lv_obj_t *lbl_clock;
  lv_timer_t *update_timer;
} main_page_ctx_t;

// 时间刷新定时器回调 (纯 C 静态函数)
static void main_page_timer_cb(lv_timer_t *t) {
  main_page_ctx_t *ctx = (main_page_ctx_t *)lv_timer_get_user_data(t);
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  // 更新数字时钟
  lv_label_set_text_fmt(ctx->lbl_clock, "%02d:%02d:%02d", timeinfo.tm_hour,
                        timeinfo.tm_min, timeinfo.tm_sec);
  // 更新日期
  lv_label_set_text_fmt(ctx->lbl_date, "%d/%02d/%02d", timeinfo.tm_year + 1900,
                        timeinfo.tm_mon + 1, timeinfo.tm_mday);
}

// 按钮点击回调：跳转到菜单页
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
  }
  return ctx;
}

static lv_obj_t *main_page_render(lv_obj_t *parent, void *ctx_in) {
  main_page_ctx_t *ctx = (main_page_ctx_t *)ctx_in;

  // 创建页面根节点
  ctx->root = lv_obj_create(parent);
  lv_obj_set_size(ctx->root, 320, 240);
  lv_obj_set_scrollbar_mode(ctx->root, LV_SCROLLBAR_MODE_OFF);

  // 倒计时标签 (对应你代码中的 main_page_daojishi)
  ctx->lbl_countdown = lv_label_create(ctx->root);
  lv_label_set_text(ctx->lbl_countdown, "25:00");
  lv_obj_set_style_text_font(ctx->lbl_countdown, &lv_font_montserrat_32, 0);
  lv_obj_align(ctx->lbl_countdown, LV_ALIGN_CENTER, 0, -40);

  // 日期标签 (对应 main_page_date_text)
  ctx->lbl_date = lv_label_create(ctx->root);
  lv_obj_add_flag(ctx->lbl_date, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(ctx->lbl_date, LV_ALIGN_CENTER, 0, 20);
  lv_obj_add_event_cb(ctx->lbl_date, main_page_date_click_cb, LV_EVENT_CLICKED,
                      NULL);

  // 数字时钟 (对应 main_page_digital_clock_1)
  ctx->lbl_clock = lv_label_create(ctx->root);
  lv_obj_align(ctx->lbl_clock, LV_ALIGN_CENTER, 0, 50);
  lv_obj_set_style_text_font(ctx->lbl_clock, &lv_font_montserrat_14, 0);

  // 启动定时器刷新时间
  ctx->update_timer = lv_timer_create(main_page_timer_cb, 1000, ctx);

  return ctx->root;
}

static void main_page_deinit(void *ctx_in) {
  main_page_ctx_t *ctx = (main_page_ctx_t *)ctx_in;
  if (ctx) {
    if (ctx->update_timer) {
      lv_timer_del(ctx->update_timer); // 风险点：必须销毁定时器
    }
    free(ctx);
  }
}

// 导出页面描述符
const gs_page_desc_t page_main = {.init_cb = main_page_init,
                                  .render_cb = main_page_render,
                                  .deinit_cb = main_page_deinit};