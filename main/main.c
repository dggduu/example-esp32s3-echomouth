#include "cam_helper.h"
#include "esp32_s3_szp.h"
#include "esp_camera.h"
#include "esp_websocket_client.h"
#include "gs_nav.h"
#include "mdns.h"
#include "my_theme.h"
#include "prov_qr.h"
#include "sntp_helper.h"
#include "wifi_prov.h"
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <stdio.h>

extern EventGroupHandle_t wifi_event_group;
extern const int WIFI_CONNECTED_EVENT;

static const char *TAG = "MAIN";

static lv_obj_t *splash_page(lv_obj_t *parent, void *ctx) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, 320, 240);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_border_width(cont, 0, 0);

  lv_obj_t *spinner = lv_spinner_create(cont);
  lv_spinner_set_anim_params(spinner, 4000, 200);
  lv_obj_set_size(spinner, 60, 60);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);
  lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(0x2195f6),
                             LV_PART_INDICATOR);

  lv_obj_t *label = lv_label_create(cont);
  lv_label_set_text(label, "初始化中...");
  lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 40);

  return cont;
}

const gs_page_desc_t page_splash = {
    .init_cb = NULL, .render_cb = splash_page, .deinit_cb = NULL};

void gui_flsuh_task(void *param) {
  while (1) {
    if (lvgl_port_lock(-1)) {
      uint32_t sleep_ms = lv_timer_handler();
      gs_nav_loop();
      lvgl_port_unlock();
      vTaskDelay(pdMS_TO_TICKS(sleep_ms <= 0 ? 1 : sleep_ms));
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

void init_mdns(const char *hostname) {
  // 初始化 mDNS 组件
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mDNS Init failed: %d", err);
    return;
  }

  ESP_ERROR_CHECK(mdns_hostname_set(hostname));
  ESP_LOGI(TAG, "mDNS hostname set to: [%s.local]", hostname);
  ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-S3 Iot"));
  mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

  mdns_service_txt_item_set("_http", "_tcp", "version", "1.0.0");
}

void query_mdns_host(const char *host_name) {
  ESP_LOGI(TAG, "Querying for [%s.local]...", host_name);

  struct esp_ip4_addr addr;
  addr.addr = 0;

  esp_err_t err = mdns_query_a(host_name, 2000, &addr);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Found host! IP: " IPSTR, IP2STR(&addr));
  } else {
    ESP_LOGE(TAG, "Host not found or Query failed.");
  }
}

static void global_service_init() {

  lv_obj_t *container = lv_scr_act();
  gs_nav_init(container);
  xTaskCreate(gui_flsuh_task, "gui", 16 * 1024, NULL, 4, NULL);
  // 加一个 Splash
  gs_nav_push(&page_splash, NULL);

  wifi_prov_init();

  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true,
                      portMAX_DELAY);
  sntp_helper_init();
  sntp_helper_set_timezone("CST-8");
  sntp_helper_time("ntp.aliyun.com", 5000);

  init_mdns("esp32-s3");
  vTaskDelay(pdMS_TO_TICKS(1000));
  query_mdns_host("aobara-pc");
}

extern const gs_page_desc_t page_cam;

#include "http_client_helper.h"

void app_main(void) {
  bsp_i2c_init();
  pca9557_init();
  bsp_lvgl_start();
  bsp_littlefs_mount();
  bsp_codec_init();
  bsp_camera_init();

  my_ui_theme_init();
  http_helper_init();

  // 全局初始化
  global_service_init();
  if (lvgl_port_lock(-1)) {
    extern const gs_page_desc_t page_main;
    gs_nav_pop();
    gs_nav_push_async(&page_main, NULL);
    lvgl_port_unlock();
  }
}