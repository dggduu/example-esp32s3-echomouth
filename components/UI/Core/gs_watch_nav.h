#ifndef GS_WATCH_NAV_H
#define GS_WATCH_NAV_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t item_count;   // 逻辑页数
  int16_t width;         // 组件宽度
  int16_t height;        // 组件高度
  bool loop_enable;      // 是否启用无限循环
  bool show_sidebar;     // 是否显示滚动条
  lv_scroll_snap_t snap; // 吸附方式，通常为 LV_SCROLL_SNAP_CENTER
} watch_nav_config_t;

typedef void (*watch_nav_render_cb)(lv_obj_t *obj, uint32_t index);

lv_obj_t *watch_nav_create(lv_obj_t *parent, const watch_nav_config_t *config,
                           watch_nav_render_cb render_fn);

#ifdef __cplusplus
}
#endif

#endif