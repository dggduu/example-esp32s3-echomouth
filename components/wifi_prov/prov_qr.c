#include "prov_qr.h"
#include "esp_log.h"
#include "gs_nav.h"
#include "gs_qrcode_comp.h"
#include "wifi_prov.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdlib.h>
#include <string.h>

#define TAG "prov_qr"
#define PROV_QR_VERSION "v1"

// 二维码显示请求结构体
typedef struct {
  char *qr_data;
  char *hint_text;
} qr_display_req_t;

// 静态队列句柄
QueueHandle_t s_qr_queue = NULL;
static bool s_qr_page_active = false;

// 页面私有上下文
typedef struct {
  char *qr_data;
  char *hint_text;
  lv_obj_t *qr_comp;
} qr_page_ctx_t;

// 状态变更回调
static void on_qr_status_changed(lv_obj_t *root, lv_obj_t *label,
                                 gs_qr_status_t status) {
  switch (status) {
  case GS_QR_WAITED:
    lv_label_set_text(label, "等待扫描中...");
    break;
  case GS_QR_SUCCESS:
    lv_label_set_text(label, "配网成功!");
    break;
  case GS_QR_FAILED:
    lv_label_set_text(label, "配网失败!");
    break;
  }
}

// 页面初始化回调
static void *qr_page_init(void *args) {
  if (!args)
    return NULL;
  const char **strings = (const char **)args;
  const char *qr_data = strings[0];
  const char *hint_text = strings[1];
  if (!qr_data)
    return NULL;

  qr_page_ctx_t *ctx = calloc(1, sizeof(qr_page_ctx_t));
  if (!ctx)
    return NULL;

  ctx->qr_data = strdup(qr_data);
  ctx->hint_text =
      hint_text ? strdup(hint_text) : strdup("Scan QR code to provision");
  s_qr_page_active = true;
  return ctx;
}

// 页面渲染回调
static lv_obj_t *qr_page_render(lv_obj_t *parent, void *ctx) {
  qr_page_ctx_t *page_ctx = (qr_page_ctx_t *)ctx;
  if (!page_ctx)
    return NULL;

  gs_qr_config_t cfg = {
      .qr_data = page_ctx->qr_data,
      .hint_text = page_ctx->hint_text,
      .on_status_changed = on_qr_status_changed,
  };
  lv_obj_t *qr_comp = gs_qrcode_comp_create(parent, &cfg);
  if (qr_comp) {
    page_ctx->qr_comp = qr_comp;
  }
  return qr_comp;
}

// 页面销毁回调
static void qr_page_deinit(void *ctx) {
  qr_page_ctx_t *page_ctx = (qr_page_ctx_t *)ctx;
  if (page_ctx) {
    free(page_ctx->qr_data);
    free(page_ctx->hint_text);
    free(page_ctx);
  }
  s_qr_page_active = false;
}

// 页面描述符
static const gs_page_desc_t qr_page_desc = {
    .init_cb = qr_page_init,
    .render_cb = qr_page_render,
    .deinit_cb = qr_page_deinit,
};

// 在 LVGL 任务中处理队列消息
static void process_qr_display(void) {
  qr_display_req_t req;
  if (s_qr_queue && xQueueReceive(s_qr_queue, &req, 0) == pdTRUE) {
    // 在 LVGL 上下文中执行页面推送
    const char *args[2] = {req.qr_data, req.hint_text};
    if (gs_nav_push(&qr_page_desc, (void *)args) != 0) {
      ESP_LOGE(TAG, "Failed to push QR page");
    }
    // 释放动态分配的内存
    free(req.qr_data);
    free(req.hint_text);
  }
}

// 应由 LVGL 任务调用一次
void prov_qr_init(void) {
  if (s_qr_queue == NULL) {
    s_qr_queue = xQueueCreate(5, sizeof(qr_display_req_t));
    ESP_LOGI(TAG, "QR display queue created");
  }
}

// 供 LVGL 任务调用的处理函数
void prov_qr_process(void) {
  if (s_qr_queue) {
    process_qr_display();
  }
}

// 显示二维码
void wifi_prov_print_qr(const char *name, const char *username, const char *pop,
                        const char *transport) {
  if (!name || !transport) {
    ESP_LOGW(TAG, "Cannot generate QR code payload. Data missing.");
    return;
  }

  if (s_qr_queue == NULL) {
    ESP_LOGE(TAG, "QR queue not initialized, call prov_qr_init() first");
    return;
  }

  char payload[150] = {0};
  if (pop) {
    snprintf(payload, sizeof(payload),
             "{\"ver\":\"%s\",\"name\":\"%s\""
             ",\"username\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
             PROV_QR_VERSION, name, username, pop, transport);
  } else {
    snprintf(payload, sizeof(payload),
             "{\"ver\":\"%s\",\"name\":\"%s\""
             ",\"transport\":\"%s\"}",
             PROV_QR_VERSION, name, transport);
  }

  // 创建请求
  qr_display_req_t req = {
      .qr_data = strdup(payload),
      .hint_text = strdup("Scan this QR code to start provisioning")};
  if (!req.qr_data || !req.hint_text) {
    free(req.qr_data);
    free(req.hint_text);
    ESP_LOGE(TAG, "Failed to allocate memory for QR request");
    return;
  }

  if (xQueueSend(s_qr_queue, &req, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to send QR request to queue");
    free(req.qr_data);
    free(req.hint_text);
  }
}

// 更新二维码状态
void prov_qr_set_status(gs_qr_status_t status) {
  if (s_qr_page_active) {
    gs_qrcode_comp_trigger(status);
  }
}