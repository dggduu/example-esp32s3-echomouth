#include "gs_qrcode_comp.h"
#include "esp_heap_caps.h" // 引入 ESP-IDF 堆内存管理头文件
#include "esp_log.h"
#include "string.h"
#include <stdlib.h>

static struct {
  lv_obj_t *root;
  lv_obj_t *qr_obj;
  lv_obj_t *label;
  gs_qr_config_t cfg;
  gs_qr_status_t current_status;
  bool is_busy;
} *s_qr = NULL;

lv_obj_t *gs_qrcode_comp_create(lv_obj_t *parent, const gs_qr_config_t *cfg) {
  if (!parent || !cfg)
    return NULL;

  /* 1. 释放上一次的结构体 */
  if (s_qr) {
    heap_caps_free(s_qr); // 匹配 heap_caps_calloc
    s_qr = NULL;
  }

  /* 2. 【关键修改】强制将 s_qr 结构体分配在 PSRAM (SPIRAM) 中，绝不抢占内部
   * DRAM */
  s_qr =
      heap_caps_calloc(1, sizeof(*s_qr), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_qr) {
    // 如果 PSRAM 没挂载成功，退回普通 calloc
    s_qr = calloc(1, sizeof(*s_qr));
    if (!s_qr)
      return NULL;
  }

  s_qr->cfg = *cfg;
  s_qr->current_status = GS_QR_WAITED;

  s_qr->root = lv_obj_create(parent);
  lv_obj_set_size(s_qr->root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_qr->root, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_border_width(s_qr->root, 0, 0);
  lv_obj_set_style_pad_all(s_qr->root, 20, 0);

  s_qr->qr_obj = lv_qrcode_create(s_qr->root);

  if (!s_qr->qr_obj) {
    ESP_LOGE("QR", "create failed");
    return NULL;
  }

  lv_qrcode_set_size(s_qr->qr_obj, 150);

  lv_qrcode_set_dark_color(s_qr->qr_obj, lv_color_black());

  lv_qrcode_set_light_color(s_qr->qr_obj, lv_color_white());

  lv_qrcode_update(s_qr->qr_obj, cfg->qr_data, strlen(cfg->qr_data));

  lv_obj_center(s_qr->qr_obj);
  ESP_LOGI("QRCODE", "qr data:%s", cfg->qr_data);

  s_qr->label = lv_label_create(s_qr->root);
  lv_obj_set_width(s_qr->label, LV_PCT(50));
  lv_label_set_text(s_qr->label, cfg->hint_text);
  lv_obj_set_style_text_align(s_qr->label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_qr->label, LV_ALIGN_BOTTOM_MID, 0, -50);

  return s_qr->root;
}

void gs_qrcode_comp_trigger(gs_qr_status_t status) {
  if (!s_qr || !s_qr->root || s_qr->is_busy)
    return;

  if (status == s_qr->current_status && status != GS_QR_WAITED)
    return;

  s_qr->is_busy = true;
  s_qr->current_status = status;

  if (s_qr->cfg.on_status_changed) {
    s_qr->cfg.on_status_changed(s_qr->root, s_qr->label, status);
  }

  s_qr->is_busy = false;
}