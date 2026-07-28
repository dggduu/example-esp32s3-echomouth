#include "prov_qr.h"
#include "esp_log.h"
#include "gs_nav.h"
#include "gs_qrcode_comp.h"
#include "mbedtls/base64.h"
#include "nvs_helper.h"
#include "wifi_prov.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdlib.h>
#include <string.h>
#define TAG "prov_qr"
#define PROV_QR_VERSION "v1"

typedef struct {
  char *qr_data;
  char *hint_text;
} qr_display_req_t;

QueueHandle_t s_qr_queue = NULL;
static bool s_qr_page_active = false;

static void set_status_async(void *arg) {
  gs_qr_status_t status = (gs_qr_status_t)(intptr_t)arg;
  if (s_qr_page_active) {
    gs_qrcode_comp_trigger(status);
  }
}

typedef struct {
  char *qr_data;
  char *hint_text;
  lv_obj_t *qr_comp;
} qr_page_ctx_t;

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

static void *qr_page_init(void *args) {
  qr_display_req_t *src = args;
  if (!src || !src->qr_data)
    return NULL;

  qr_page_ctx_t *ctx = calloc(1, sizeof(qr_page_ctx_t));
  if (!ctx)
    return NULL;

  ctx->qr_data = src->qr_data; /* take ownership of strdup'd data */
  ctx->hint_text =
      src->hint_text ? src->hint_text : strdup("Scan QR to provision");
  free(src); /* free the request envelope */
  s_qr_page_active = true;
  return ctx;
}

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
  ESP_LOGI(TAG, "page render");
  ESP_LOGI(TAG, "page init");
  if (!qr_comp) {
    ESP_LOGE(TAG, "QR component create failed");
  } else {
    ESP_LOGI(TAG, "QR component created");
  }
  if (qr_comp) {
    page_ctx->qr_comp = qr_comp;
  }
  return qr_comp;
}

static void qr_page_deinit(void *ctx) {
  qr_page_ctx_t *page_ctx = (qr_page_ctx_t *)ctx;
  if (page_ctx) {
    free(page_ctx->qr_data);
    free(page_ctx->hint_text);
    free(page_ctx);
  }
  s_qr_page_active = false;
}

static const gs_page_desc_t qr_page_desc = {
    .init_cb = qr_page_init,
    .render_cb = qr_page_render,
    .deinit_cb = qr_page_deinit,
};

static void process_qr_display(void) {
  qr_display_req_t *req = malloc(sizeof(qr_display_req_t));
  if (!req)
    return;

  if (xQueueReceive(s_qr_queue, req, 0) == pdTRUE) {
    gs_nav_push(&qr_page_desc, req);
  } else {
    free(req);
  }
}

void prov_qr_init(void) {
  if (s_qr_queue == NULL) {
    s_qr_queue = xQueueCreate(5, sizeof(qr_display_req_t));
    ESP_LOGI(TAG, "QR display queue created");
  }
}

void prov_qr_process(void) {
  if (s_qr_queue) {
    process_qr_display();
  }
}

void wifi_prov_print_qr(const char *name, const char *username, const char *pop,
                        const char *transport) {
  if (!name || !transport) {
    ESP_LOGW(TAG, "无法生成二维码");
    return;
  }
  if (s_qr_queue == NULL) {
    ESP_LOGE(TAG, "QR 队列未初始化");
    return;
  }

  // 读取设备派生密钥
  char dev_key_b64[25] = {0};
  uint8_t device_key[16];
  if (nvs_helper_get_device_key(device_key) == ESP_OK) {
    size_t b64_len = 0;
    mbedtls_base64_encode((unsigned char *)dev_key_b64, sizeof(dev_key_b64),
                          &b64_len, device_key, 16);
    dev_key_b64[b64_len] = '\0';
  } else {
    ESP_LOGW(TAG, "Device key not found in NVS, QR will not contain devKey");
  }

  char payload[256] = {0};
  int cur_len = 0;

  // 1. 拼接基础信息
  if (pop) {
    cur_len = snprintf(payload, sizeof(payload),
                       "{\"ver\":\"%s\",\"name\":\"%s\",\"username\":\"%s\","
                       "\"pop\":\"%s\",\"transport\":\"%s\"",
                       PROV_QR_VERSION, name, username ? username : "", pop,
                       transport);
  } else {
    cur_len = snprintf(payload, sizeof(payload),
                       "{\"ver\":\"%s\",\"name\":\"%s\",\"transport\":\"%s\"",
                       PROV_QR_VERSION, name, transport);
  }

  if (strlen(dev_key_b64) > 0 && cur_len < sizeof(payload)) {
    cur_len += snprintf(payload + cur_len, sizeof(payload) - cur_len,
                        ",\"devKey\":\"%s\"", dev_key_b64);
  }

  if (cur_len < sizeof(payload)) {
    snprintf(payload + cur_len, sizeof(payload) - cur_len,
             "}"); // 补齐闭合大括号
  }

  qr_display_req_t req = {.qr_data = strdup(payload),
                          .hint_text = strdup("APP扫码")};
  if (!req.qr_data || !req.hint_text) {
    free(req.qr_data);
    free(req.hint_text);
    ESP_LOGE(TAG, "QR 内存分配失败");
    return;
  }

  if (xQueueSend(s_qr_queue, &req, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "QR 发送队列失败");
    free(req.qr_data);
    free(req.hint_text);
  }
}

void prov_qr_set_status(gs_qr_status_t status) {
  lv_async_call(set_status_async, (void *)(intptr_t)status);
}