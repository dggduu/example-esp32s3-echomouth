#include "gs_qrcode_comp.h"
#include "string.h"

static struct {
  lv_obj_t *root;
  lv_obj_t *qr_obj;
  lv_obj_t *label;
  gs_qr_config_t cfg;
  gs_qr_status_t current_status; // 记录状态
  bool is_busy;                  // 递归保护锁
} s_comp;

lv_obj_t *gs_qrcode_comp_create(lv_obj_t *parent, const gs_qr_config_t *cfg) {
  if (!parent || !cfg)
    return NULL;

  s_comp.cfg = *cfg;
  s_comp.current_status = GS_QR_WAITED;
  s_comp.is_busy = false;

  s_comp.root = lv_obj_create(parent);
  lv_obj_set_size(s_comp.root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_comp.root, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_border_width(s_comp.root, 0, 0);
  lv_obj_set_style_pad_all(s_comp.root, 0, 0);

  s_comp.qr_obj = lv_qrcode_create(s_comp.root);
  lv_qrcode_set_size(s_comp.qr_obj, 120);
  lv_qrcode_update(s_comp.qr_obj, cfg->qr_data, strlen(cfg->qr_data));
  lv_obj_align(s_comp.qr_obj, LV_ALIGN_CENTER, 0, -20);

  s_comp.label = lv_label_create(s_comp.root);
  lv_obj_set_width(s_comp.label, LV_PCT(80));
  lv_label_set_text(s_comp.label, cfg->hint_text);
  lv_obj_set_style_text_align(s_comp.label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_comp.label, LV_ALIGN_BOTTOM_MID, 0, -30);

  return s_comp.root;
}

void gs_qrcode_comp_trigger(gs_qr_status_t status) {
  if (!s_comp.root || s_comp.is_busy)
    return;

  // 状态重复检查
  if (status == s_comp.current_status && status != GS_QR_WAITED)
    return;

  s_comp.is_busy = true; // 上锁
  s_comp.current_status = status;

  if (s_comp.cfg.on_status_changed) {
    s_comp.cfg.on_status_changed(s_comp.root, s_comp.label, status);
  }

  s_comp.is_busy = false; // 解锁
}