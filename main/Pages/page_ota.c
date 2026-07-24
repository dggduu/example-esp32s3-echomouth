#include "page_ota.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "gs_nav.h"
#include "manager.h"
#include "ota_backend.h"
#include "StyleSheet.h"
#include <stdio.h>
#include <stdlib.h>

#include "esp_bt.h"
#include "freertos/event_groups.h"

static const char *TAG = "PAGE_OTA";

typedef struct {
  lv_obj_t *bar_progress;
  lv_obj_t *lbl_percent;
  lv_obj_t *lbl_status;
} page_ota_ctx_t;

typedef struct {
  uint32_t current_bytes;
  uint32_t total_bytes;
  ota_state_t state;
  char status[64];
  bool progress_dirty;
  bool status_dirty;
} ota_ui_data_t;

static ota_ui_data_t s_ui_data = {.state = OTA_STATE_IDLE};
static page_ota_ctx_t *s_ota_ui_ctx = NULL;

void page_ota_notify_progress(uint32_t current_bytes, uint32_t total_bytes) {
  s_ui_data.current_bytes = current_bytes;
  s_ui_data.total_bytes = total_bytes;
  s_ui_data.progress_dirty = true;
}

void page_ota_notify_status(const char *status_msg, int state) {
  if (!status_msg) return;
  s_ui_data.state = state;
  strncpy(s_ui_data.status, status_msg, sizeof(s_ui_data.status) - 1);
  s_ui_data.status[sizeof(s_ui_data.status) - 1] = '\0';
  s_ui_data.status_dirty = true;
}

static void *page_ota_init(void *args) {
  page_ota_ctx_t *ctx = calloc(1, sizeof(page_ota_ctx_t));
  if (!ctx) { gs_nav_pop(); return NULL; }

  if (!ota_backend_init()) {
    ESP_LOGE(TAG, "Failed to init OTA backend");
    free(ctx);
    return NULL;
  }
  s_ota_ui_ctx = ctx;
  return ctx;
}

static void page_ota_update(void *ctx_ptr) {
  page_ota_ctx_t *ctx = ctx_ptr;
  if (!ctx) return;

  if (s_ui_data.progress_dirty) {
    s_ui_data.progress_dirty = false;
    uint32_t percent = s_ui_data.total_bytes
        ? s_ui_data.current_bytes * 100 / s_ui_data.total_bytes : 0;
    lv_bar_set_value(ctx->bar_progress, percent, LV_ANIM_ON);
    lv_label_set_text_fmt(ctx->lbl_percent, "%lu%%", percent);
  }

  if (s_ui_data.status_dirty) {
    s_ui_data.status_dirty = false;
    lv_label_set_text(ctx->lbl_status, s_ui_data.status);
  }
}

static lv_obj_t *page_ota_render(lv_obj_t *parent, void *ctx_ptr) {
  page_ota_ctx_t *ctx = (page_ota_ctx_t *)ctx_ptr;
  if (!ctx) return NULL;

  lv_obj_t *page = lv_obj_create(parent);
  lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(page, 28, 0);
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  /* icon area */
  lv_obj_t *icon = lv_label_create(page);
  lv_label_set_text(icon, LV_SYMBOL_DOWNLOAD);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(icon, S_COLOR_PRIMARY, 0);

  /* title */
  lv_obj_t *lbl_title = lv_label_create(page);
  lv_label_set_text(lbl_title, "固件更新");
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl_title, S_TEXT_PRIMARY, 0);
  lv_obj_set_style_margin_top(lbl_title, S_GAP, 0);

  /* progress bar */
  ctx->bar_progress = lv_bar_create(page);
  lv_obj_set_size(ctx->bar_progress, LV_PCT(70), 12);
  lv_bar_set_range(ctx->bar_progress, 0, 100);
  lv_bar_set_value(ctx->bar_progress, 0, LV_ANIM_OFF);
  lv_obj_set_style_margin_top(ctx->bar_progress, S_PAD_H * 2, 0);

  ctx->lbl_percent = lv_label_create(page);
  lv_label_set_text(ctx->lbl_percent, "0%");
  lv_obj_set_style_text_color(ctx->lbl_percent, S_TEXT_SECONDARY, 0);
  lv_obj_set_style_margin_top(ctx->lbl_percent, S_GAP, 0);

  ctx->lbl_status = lv_label_create(page);
  lv_label_set_text(ctx->lbl_status, "等待连接...");
  lv_obj_set_style_text_color(ctx->lbl_status, S_TEXT_SECONDARY, 0);
  lv_obj_set_style_margin_top(ctx->lbl_status, S_GAP, 0);

  return page;
}

static void page_ota_deinit(void *ctx_ptr) {
  ota_backend_deinit();
  s_ota_ui_ctx = NULL;
  free(ctx_ptr);
}

const gs_page_desc_t page_ota = {
    .init_cb = page_ota_init,
    .render_cb = page_ota_render,
    .deinit_cb = page_ota_deinit,
    .update_cb = page_ota_update,
};
