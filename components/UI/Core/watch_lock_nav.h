#ifndef WATCH_LOCK_NAV_H
#define WATCH_LOCK_NAV_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// 定义视图创建回调类型
typedef lv_obj_t *(*gs_view_cb)(lv_obj_t *parent);

typedef struct {
  uint32_t dir_mask; // 手势方向掩码，支持 LV_DIR_TOP/BOTTOM/LEFT/RIGHT 按位或
  bool click_unlock; // 是否支持点击解锁
} unlock_config_t;

int watch_lock_nav_start(gs_view_cb lock_hook, gs_view_cb main_hook,
                         const unlock_config_t *config);

#ifdef __cplusplus
}
#endif

#endif