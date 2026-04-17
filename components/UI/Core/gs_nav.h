#ifndef GS_NAV_H
#define GS_NAV_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void *(*init_cb)(void *args);
  lv_obj_t *(*render_cb)(lv_obj_t *parent, void *ctx);
  void (*update_cb)(void *ctx);
  void (*deinit_cb)(void *ctx);
} gs_page_desc_t;

void gs_nav_init(lv_obj_t *container);
int gs_nav_push(const gs_page_desc_t *page, void *args);
int gs_nav_pop(void);
void gs_nav_loop(void);
int gs_nav_depth(void);
void gs_nav_push_async(const gs_page_desc_t *page, void *args);
void gs_nav_pop_async(void);

#ifdef __cplusplus
}
#endif

#endif