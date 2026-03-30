#include "esp32_s3_szp.h"
#include "gs_nav.h"
#include "prov_qr.h"
#include "sntp_helper.h"
#include "wifi_prov.h"
#include <esp_wifi.h>
#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

extern EventGroupHandle_t wifi_event_group;
extern const int WIFI_CONNECTED_EVENT;

static lv_obj_t *splash_page(lv_obj_t *parent, void *ctx) {
  // 创建背景容器
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, 320, 240);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_border_width(cont, 0, 0);

  // 创建旋转加载动画
  lv_obj_t *spinner = lv_spinner_create(cont);
  lv_spinner_set_anim_params(spinner, 4000, 200);
  lv_obj_set_size(spinner, 60, 60);

  lv_obj_set_size(spinner, 60, 60);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);
  lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(0x2195f6),
                             LV_PART_INDICATOR);

  // 加载提示文字
  lv_obj_t *label = lv_label_create(cont);
  lv_label_set_text(label, "System Initializing...");
  lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 40);

  return cont;
}

const gs_page_desc_t page_splash = {
    .init_cb = NULL, .render_cb = splash_page, .deinit_cb = NULL};

static void global_service_init() {
  lv_obj_t *container = lv_scr_act();
  gs_nav_init(container);
  // 加一个 Splash
  gs_nav_push(&page_splash, NULL);

  wifi_prov_init();

  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true,
                      portMAX_DELAY);
  sntp_helper_init();
  sntp_helper_set_timezone("CST-8");
  sntp_helper_time("ntp.aliyun.com", 5000);
}

void app_main(void) {
  bsp_i2c_init();   // I2C初始化
  pca9557_init();   // IO扩展芯片初始化
  bsp_lvgl_start(); // 初始化液晶屏lvgl接口

  bsp_littlefs_mount(); // SPIFFS文件系统初始化
  bsp_codec_init();     // 音频初始化

  // 全局初始化
  global_service_init();

  if (lvgl_port_lock(1000)) {
    extern const gs_page_desc_t page_main;
    gs_nav_pop();
    gs_nav_push(&page_main, NULL);
    lvgl_port_unlock();
  }
}