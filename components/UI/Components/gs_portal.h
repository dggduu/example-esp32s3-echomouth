#ifndef GS_PORTAL_H
#define GS_PORTAL_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GS_TOAST_SUCCESS,
  GS_TOAST_FAILED,
  GS_TOAST_INFO
} gs_toast_type_t;

typedef struct {
  const char *title;
  const char *msg;
  uint32_t anim_in_time;
  uint32_t anim_out_time;
  int win_w;
  int win_h;
  void (*ok_cb)(void *user_data);
  void (*cancel_cb)(void *user_data);
  void *user_data;
} gs_alert_config_t;

typedef struct {
  const char *msg;
  gs_toast_type_t type;
  uint32_t anim_in_time;
  uint32_t anim_out_time;
  uint32_t stay_time;
  void (*click_cb)(void *user_data);
  void *user_data;
} gs_toast_config_t;

#define GS_TOAST_DEFAULT_CONFIG()                                              \
  {.msg = "",                                                                  \
   .type = GS_TOAST_INFO,                                                      \
   .stay_time = 2000,                                                          \
   .click_cb = NULL,                                                           \
   .user_data = NULL}

#define GS_ALERT_DEFAULT_CONFIG()                                              \
  {.title = "",                                                                \
   .msg = "",                                                                  \
   .win_w = 280,                                                               \
   .win_h = 160,                                                               \
   .ok_cb = NULL,                                                              \
   .cancel_cb = NULL,                                                          \
   .user_data = NULL}

void gs_portal_toast_show(gs_toast_config_t cfg);
/* 立即关闭当前 Toast */
void gs_portal_toast_dismiss(void);

/* 显示 Alert 对话框 */
void gs_portal_alert_show(gs_alert_config_t cfg);
/* 立即关闭当前 Alert */
void gs_portal_alert_dismiss(void);

/* 便捷函数 */
void gs_toast_show(const char *msg, gs_toast_type_t type);
void gs_alert_show(const char *title, const char *msg);

#ifdef __cplusplus
}
#endif

#endif