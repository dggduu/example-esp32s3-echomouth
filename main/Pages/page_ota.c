#include "page_ota.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "gs_nav.h"
#include "manager.h"
#include "ota_backend.h"
#include <stdio.h>
#include <stdlib.h>

#include "esp_bt.h"

#include "freertos/event_groups.h"

static const char *TAG = "PAGE_OTA";

typedef struct {
  lv_obj_t *bar_progress;
  lv_obj_t *lbl_percent;
  lv_obj_t *lbl_status;
  lv_obj_t *btn_exit;
} page_ota_ctx_t;

typedef struct {
  uint32_t current_bytes;
  uint32_t total_bytes;
  ota_state_t state;
  char status[64];
  bool progress_dirty;
  bool status_dirty;
} ota_ui_data_t;

static ota_ui_data_t s_ui_data = {
    .state = OTA_STATE_IDLE,
};

static page_ota_ctx_t *s_ota_ui_ctx = NULL;

static void btn_exit_event_cb(lv_event_t *e) { gs_nav_pop_async(); }

void page_ota_notify_progress(uint32_t current_bytes, uint32_t total_bytes) {
  s_ui_data.current_bytes = current_bytes;
  s_ui_data.total_bytes = total_bytes;
  s_ui_data.progress_dirty = true;
}

void page_ota_notify_status(const char *status_msg, int state) {
  if (!status_msg)
    return;

  s_ui_data.state = state;

  strncpy(s_ui_data.status, status_msg, sizeof(s_ui_data.status) - 1);

  s_ui_data.status[sizeof(s_ui_data.status) - 1] = '\0';

  s_ui_data.status_dirty = true;
}

static void *page_ota_init(void *args) {
  ESP_LOGI(TAG, "jump to page ota,initing....");

  ESP_LOGI(TAG, "ble not busy, start ota comp");

  page_ota_ctx_t *ctx = (page_ota_ctx_t *)calloc(1, sizeof(page_ota_ctx_t));

  if (!ctx) {
    gs_nav_pop();
    return NULL;
  }

  // 启动 OTA 后台任务
  if (!ota_backend_init()) {
    ESP_LOGE(TAG, "Failed to init OTA backend");
    free(ctx);
    return NULL;
  }

  ESP_LOGI(TAG, "page ota init end");

  s_ota_ui_ctx = ctx;
  return ctx;
}

static void page_ota_update(void *ctx_ptr) {
  page_ota_ctx_t *ctx = ctx_ptr;

  if (!ctx)
    return;

  if (s_ui_data.progress_dirty) {

    s_ui_data.progress_dirty = false;

    uint32_t percent = 0;

    if (s_ui_data.total_bytes != 0) {
      percent = s_ui_data.current_bytes * 100 / s_ui_data.total_bytes;
    }

    lv_bar_set_value(ctx->bar_progress, percent, LV_ANIM_OFF);

    lv_label_set_text_fmt(ctx->lbl_percent, "%lu%%", percent);
  }

  if (s_ui_data.status_dirty) {

    s_ui_data.status_dirty = false;

    lv_label_set_text(ctx->lbl_status, s_ui_data.status);

    if (ctx->btn_exit) {

      if (s_ui_data.state == OTA_STATE_CONNECTED ||
          s_ui_data.state == OTA_STATE_TRANSFERRING) {

        lv_obj_add_state(ctx->btn_exit, LV_STATE_DISABLED);
      } else {

        lv_obj_clear_state(ctx->btn_exit, LV_STATE_DISABLED);
      }
    }
  }
}

static lv_obj_t *page_ota_render(lv_obj_t *parent, void *ctx_ptr) {
  page_ota_ctx_t *ctx = (page_ota_ctx_t *)ctx_ptr;
  if (!ctx)
    return NULL;

  lv_obj_t *page = lv_obj_create(parent);
  lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));

  // ctx->btn_exit = lv_btn_create(page);
  // lv_obj_align(ctx->btn_exit, LV_ALIGN_TOP_LEFT, 10, 10);
  // lv_obj_add_event_cb(ctx->btn_exit, btn_exit_event_cb, LV_EVENT_CLICKED,
  // NULL); lv_obj_t *lbl_btn = lv_label_create(ctx->btn_exit);
  // lv_label_set_text(lbl_btn, "Exit");

  lv_obj_t *lbl_title = lv_label_create(page);
  lv_label_set_text(lbl_title, "固件更新");
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 20);

  ctx->bar_progress = lv_bar_create(page);
  lv_obj_set_size(ctx->bar_progress, 200, 20);
  lv_obj_align(ctx->bar_progress, LV_ALIGN_CENTER, 0, -10);
  lv_bar_set_range(ctx->bar_progress, 0, 100);
  lv_bar_set_value(ctx->bar_progress, 0, LV_ANIM_OFF);

  ctx->lbl_percent = lv_label_create(page);
  lv_label_set_text(ctx->lbl_percent, "0%");
  lv_obj_align_to(ctx->lbl_percent, ctx->bar_progress, LV_ALIGN_OUT_BOTTOM_MID,
                  0, 10);

  ctx->lbl_status = lv_label_create(page);
  lv_label_set_text(ctx->lbl_status, "等待 APP 连接...");
  lv_obj_align_to(ctx->lbl_status, ctx->lbl_percent, LV_ALIGN_OUT_BOTTOM_MID, 0,
                  20);

  return page;
}

static void page_ota_deinit(void *ctx_ptr) {
  // 停止 OTA 后台任务，释放资源
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