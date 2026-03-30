#ifndef GS_TAB_NAV_H
#define GS_TAB_NAV_H

#include "gs_nav.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// typedef struct {
//   void *(*init_cb)(void *args);
//   lv_obj_t *(*render_cb)(lv_obj_t *parent, void *ctx);
//   void (*deinit_cb)(void *ctx);
// } gs_page_desc_t;

// 确保这里的函数名与 main.c 调用的一致
void gs_tab_nav_init(lv_obj_t *parent);
int gs_tab_nav_add_page(const gs_page_desc_t *page, void *args);
void gs_tab_nav_set_index(uint32_t index, lv_anim_enable_t anim);

#ifdef __cplusplus
}
#endif

#endif