#include "gs_watch_nav.h"
#include <string.h>

/* 内部上下文，保存配置和回调 */
typedef struct {
  watch_nav_config_t config;
  watch_nav_render_cb render_fn;
  uint32_t total_pages; // 实际创建的总页面数（loop 时多两个副本）
} nav_context_t;

/* 滚动事件回调（处理无限循环边界） */
static void on_scroll_event(lv_event_t *e) {
  lv_obj_t *cont = lv_event_get_target(e);
  nav_context_t *ctx = (nav_context_t *)lv_obj_get_user_data(cont);
  if (!ctx || !ctx->config.loop_enable)
    return;

  int32_t scroll_y = lv_obj_get_scroll_y(cont);
  int32_t page_h = ctx->config.height;
  uint32_t total = ctx->config.item_count;

  if (scroll_y <= 0) {
    // 滚动到顶部边界，跳转到对应副本位置
    lv_obj_scroll_to_y(cont, total * page_h, LV_ANIM_OFF);
  } else if (scroll_y >= (total + 1) * page_h) {
    // 滚动到底部边界，跳转到对应副本位置
    lv_obj_scroll_to_y(cont, page_h, LV_ANIM_OFF);
  }
}

lv_obj_t *watch_nav_create(lv_obj_t *parent, const watch_nav_config_t *config,
                           watch_nav_render_cb render_fn) {
  if (!parent || !config || config->item_count == 0)
    return NULL;

  // 创建容器
  lv_obj_t *cont = lv_obj_create(parent);
  if (!cont)
    return NULL;

  lv_obj_set_size(cont, config->width, config->height);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_row(cont, 0, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);

  // 滚动条设置
  if (config->show_sidebar) {
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_anim_duration(cont, 200, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(cont, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(cont, lv_palette_main(LV_PALETTE_GREY),
                              LV_PART_SCROLLBAR);
  } else {
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
  }

  lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_set_scroll_snap_y(cont, config->snap);

  // 分配上下文并关联到容器
  nav_context_t *ctx = (nav_context_t *)lv_malloc(sizeof(nav_context_t));
  if (!ctx) {
    lv_obj_del(cont);
    return NULL;
  }
  memcpy(&ctx->config, config, sizeof(watch_nav_config_t));
  ctx->render_fn = render_fn;
  ctx->total_pages =
      config->loop_enable ? config->item_count + 2 : config->item_count;

  lv_obj_set_user_data(cont, ctx);

  // 创建页面
  int start_idx = config->loop_enable ? -1 : 0;
  int end_idx = config->loop_enable ? (int)config->item_count
                                    : (int)config->item_count - 1;

  for (int i = start_idx; i <= end_idx; i++) {
    uint32_t logic_idx;
    if (i == -1)
      logic_idx = config->item_count - 1; // 前副本
    else if (i == (int)config->item_count)
      logic_idx = 0; // 后副本
    else
      logic_idx = i;

    lv_obj_t *page = lv_obj_create(cont);
    if (!page) {
      // 创建失败，清理已创建的对象
      lv_obj_del(cont);
      lv_free(ctx);
      return NULL;
    }
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    if (render_fn) {
      render_fn(page, logic_idx);
    }
  }

  // 添加滚动事件，用于无限循环边界修正
  if (config->loop_enable) {
    lv_obj_add_event_cb(cont, on_scroll_event, LV_EVENT_SCROLL_END, NULL);
  }

  lv_obj_update_layout(cont);

  // 初始定位到第一页（loop 模式下定位到中间副本）
  if (config->loop_enable) {
    lv_obj_scroll_to_y(cont, config->height, LV_ANIM_OFF);
  }

  return cont;
}