#ifndef GS_WATCH_NAV_H
#define GS_WATCH_NAV_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 轮播组件配置
 */
typedef struct {
  uint32_t item_count;   // 逻辑页数
  int16_t width;         // 组件宽度
  int16_t height;        // 组件高度
  bool loop_enable;      // 是否启用无限循环
  bool show_sidebar;     // 是否显示滚动条
  lv_scroll_snap_t snap; // 吸附方式，通常为 LV_SCROLL_SNAP_CENTER
} watch_nav_config_t;

/**
 * @brief 页面渲染回调
 * @param obj 页面对象（由组件内部创建）
 * @param index 逻辑索引 (0 ~ item_count-1)
 */
typedef void (*watch_nav_render_cb)(lv_obj_t *obj, uint32_t index);

/**
 * @brief 创建轮播组件
 * @param parent 父对象
 * @param config 配置参数
 * @param render_fn 渲染回调
 * @return 组件对象，失败返回 NULL
 */
lv_obj_t *watch_nav_create(lv_obj_t *parent, const watch_nav_config_t *config,
                           watch_nav_render_cb render_fn);

#ifdef __cplusplus
}
#endif

#endif