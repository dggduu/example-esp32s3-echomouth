#ifndef GS_QRCODE_COMP_H
#define GS_QRCODE_COMP_H

#include "lvgl.h"

typedef enum { GS_QR_WAITED, GS_QR_SUCCESS, GS_QR_FAILED } gs_qr_status_t;

typedef void (*gs_qr_status_cb)(lv_obj_t *root, lv_obj_t *label,
                                gs_qr_status_t status);

typedef struct {
  const char *qr_data;
  const char *hint_text;
  gs_qr_status_cb on_status_changed;
} gs_qr_config_t;

lv_obj_t *gs_qrcode_comp_create(lv_obj_t *parent, const gs_qr_config_t *cfg);
void gs_qrcode_comp_trigger(gs_qr_status_t status);

#endif