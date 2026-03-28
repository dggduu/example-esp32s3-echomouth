#ifndef GS_QRCODE_COMP_H
#define GS_QRCODE_COMP_H

#include "lvgl.h"

typedef enum {
  GS_QR_WAITED,  // 等待扫描
  GS_QR_SUCCESS, // 逻辑成功
  GS_QR_FAILED   // 逻辑失败
} gs_qr_status_t;

/**
 * 状态变更回调：完全由外部决定 UI 表现
 * @param root  组件根容器指针（可改背景等）
 * @param label 组件提示语 Label 指针（可改文字）
 * @param status 当前变换到的状态
 */
typedef void (*gs_qr_status_cb)(lv_obj_t *root, lv_obj_t *label,
                                gs_qr_status_t status);

typedef struct {
  const char *qr_data;
  const char *hint_text;
  gs_qr_status_cb on_status_changed; // 唯一的行为入口
} gs_qr_config_t;

lv_obj_t *gs_qrcode_comp_create(lv_obj_t *parent, const gs_qr_config_t *cfg);
void gs_qrcode_comp_trigger(gs_qr_status_t status);

#endif