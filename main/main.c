#include "esp32_s3_szp.h"
#include "gs_nav.h"
#include "sntp_helper.h"
#include <stdio.h>

#include "wifi_prov.h"

typedef struct {
  int dummy;
} page_a_ctx_t;

typedef struct {
  int counter;
} page_b_ctx_t;

/* 前向声明页面描述符，因为回调中需要用到 */
extern const gs_page_desc_t page_a_desc;
extern const gs_page_desc_t page_b_desc;

/* 页面 A 的事件回调*/
static void page_a_btn_event_cb(lv_event_t *e) {
  gs_nav_push(&page_b_desc, NULL);
}

static void *page_a_init(void *args) {
  page_a_ctx_t *ctx = (page_a_ctx_t *)malloc(sizeof(page_a_ctx_t));
  if (!ctx)
    return NULL;
  ctx->dummy = 0;
  return ctx;
}

/* 页面 B 初始化 */
static void *page_b_init(void *args) {
  page_b_ctx_t *ctx = (page_b_ctx_t *)malloc(sizeof(page_b_ctx_t));
  if (!ctx)
    return NULL;
  ctx->counter = 0;
  return ctx;
}

/* 页面 B 的事件回调 */
static void page_b_btn_event_cb(lv_event_t *e) { gs_nav_pop(); }

static lv_obj_t *page_a_render(lv_obj_t *parent, void *ctx) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(cont, lv_palette_main(LV_PALETTE_GREEN), 0);

  lv_obj_t *label = lv_label_create(cont);
  lv_label_set_text(label, "Page A\nClick button to go to Page B");
  lv_obj_center(label);

  lv_obj_t *btn = lv_btn_create(cont);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_t *btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, "Go to Page B");

  lv_obj_add_event_cb(btn, page_a_btn_event_cb, LV_EVENT_CLICKED, NULL);
  return cont;
}

static void page_a_deinit(void *ctx) {
  if (ctx)
    free(ctx);
}

static lv_obj_t *page_b_render(lv_obj_t *parent, void *ctx) {
  page_b_ctx_t *b_ctx = (page_b_ctx_t *)ctx;
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(cont, lv_palette_main(LV_PALETTE_ORANGE), 0);

  lv_obj_t *label = lv_label_create(cont);
  lv_label_set_text_fmt(label, "Page B\nCounter: %d", b_ctx->counter);
  lv_obj_center(label);

  lv_obj_t *btn = lv_btn_create(cont);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_t *btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, "Back");

  lv_obj_add_event_cb(btn, page_b_btn_event_cb, LV_EVENT_CLICKED, NULL);
  return cont;
}

static void page_b_deinit(void *ctx) {
  if (ctx)
    free(ctx);
}

/* 定义页面描述符 */
const gs_page_desc_t page_a_desc = {
    .init_cb = page_a_init,
    .render_cb = page_a_render,
    .deinit_cb = page_a_deinit,
};
const gs_page_desc_t page_b_desc = {
    .init_cb = page_b_init,
    .render_cb = page_b_render,
    .deinit_cb = page_b_deinit,
};

#include "prov_qr.h"

void lvgl_task(void *arg) {
  prov_qr_init();
  while (1) {
    prov_qr_process();
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

static void test_gs_nav(void) {
  lv_obj_t *container = lv_scr_act();
  gs_nav_init(container);
  wifi_prov_nvs_init();

  xTaskCreate(lvgl_task, "lvgl", 4096, NULL, 4, NULL);
  wifi_prov_init();

  vTaskDelay(pdMS_TO_TICKS(10000));

  sntp_helper_init();

  // 设置时区为中国标准时间
  sntp_helper_set_timezone("CST-8");

  // 同步时间（阻塞 10 秒）
  if (sntp_helper_time("pool.ntp.org", 10000) == ESP_OK) {
    // 获取当前毫秒时间戳
    uint64_t now_ms = sntp_helper_get_timestamp_ms();
    ESP_LOGI("main", "Current timestamp (ms): %llu", now_ms);

    // 获取本地时间字符串
    time_t now_s = now_ms / 1000;
    struct tm tm_info;
    localtime_r(&now_s, &tm_info);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    ESP_LOGI("main", "Local time: %s", buf);
  } else {
    ESP_LOGE("main", "Time sync failed");
  }
}

void app_main(void) {
  bsp_i2c_init();   // I2C初始化
  pca9557_init();   // IO扩展芯片初始化
  bsp_lvgl_start(); // 初始化液晶屏lvgl接口

  bsp_littlefs_mount(); // SPIFFS文件系统初始化
  bsp_codec_init();     // 音频初始化

  test_gs_nav();
}