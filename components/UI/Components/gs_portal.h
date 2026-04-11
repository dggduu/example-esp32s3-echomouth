#ifndef GS_PORTAL_H
#define GS_PORTAL_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 动画配置宏 */
#define PORTAL_ANIM_DURATION_IN 300    /* 进入动画时长(ms) */
#define PORTAL_ANIM_DURATION_OUT 300   /* 退出动画时长(ms) */
#define PORTAL_TOAST_STAY_DEFAULT 2000 /* Toast 默认停留时长(ms) */
/* 动画曲线函数（由 easing.h 提供） */
#define PORTAL_ANIM_IN_EASE CubicEaseOut
#define PORTAL_ANIM_OUT_EASE CubicEaseIn

/* Toast 类型 */
typedef enum {
  GS_TOAST_SUCCESS,
  GS_TOAST_FAILED,
  GS_TOAST_INFO
} gs_toast_type_t;

/* Alert 配置 */
typedef struct {
  const char *title;                  /* 标题 */
  const char *msg;                    /* 内容 */
  int32_t win_w;                      /* 窗口宽度，<=0 使用默认值 */
  int32_t win_h;                      /* 窗口高度，<=0 使用默认值 */
  uint32_t anim_in_time;              /* 进入动画时长，0 使用宏默认值 */
  uint32_t anim_out_time;             /* 退出动画时长，0 使用宏默认值 */
  void (*ok_cb)(void *user_data);     /* 确定回调 */
  void (*cancel_cb)(void *user_data); /* 取消回调 */
  void *user_data;                    /* 用户数据 */
} gs_alert_config_t;

/* Toast 配置 */
typedef struct {
  const char *msg;                   /* 内容 */
  gs_toast_type_t type;              /* 类型（影响颜色） */
  uint32_t stay_time;                /* 停留时长，0 使用宏默认值 */
  uint32_t anim_in_time;             /* 进入动画时长，0 使用宏默认值 */
  uint32_t anim_out_time;            /* 退出动画时长，0 使用宏默认值 */
  void (*click_cb)(void *user_data); /* 点击回调（可选） */
  void *user_data;                   /* 用户数据 */
} gs_toast_config_t;

/* 显示 Alert */
void gs_portal_alert_show(gs_alert_config_t cfg);

/* 显示 Toast */
void gs_portal_toast_show(gs_toast_config_t cfg);

/* ========== 默认配置宏 ========== */
#define GS_ALERT_DEFAULT_CONFIG()                                              \
  (gs_alert_config_t) {                                                        \
    .title = NULL, .msg = NULL, .win_w = 280, /* 默认宽度 */                   \
        .win_h = 160,                         /* 默认高度 */                   \
        .anim_in_time = 200,                  /* 默认进入动画时长 200ms */     \
        .anim_out_time = 200,                 /* 默认退出动画时长 200ms */     \
        .ok_cb = NULL, .cancel_cb = NULL, .user_data = NULL                    \
  }

#define GS_TOAST_DEFAULT_CONFIG()                                              \
  (gs_toast_config_t) {                                                        \
    .msg = NULL, .type = GS_TOAST_INFO, /* 默认信息类型 */                     \
        .stay_time = 2000,              /* 默认停留 2000ms */                  \
        .anim_in_time = 200,            /* 默认进入动画 200ms */               \
        .anim_out_time = 200,           /* 默认退出动画 200ms */               \
        .click_cb = NULL, .user_data = NULL                                    \
  }

void gs_alert_show(const char *title, const char *msg);

void gs_toast_show(const char *msg, gs_toast_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* GS_PORTAL_H */