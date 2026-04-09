#include "esp_cpu.h"
#include "esp_log.h"
#include "stdio.h"
#include <math.h>
#include <stdio.h>

#include "face_detector_helper.h"

#include "esp32_s3_szp.h"

static const char *TAG = "FACE_DETECTION";

void app_main(void) {
  bsp_i2c_init();
  pca9557_init();
  bsp_lcd_init();
  bsp_camera_init();

  face_detector_helper_init();
  while (1) {
    if (!face_detector_helper_is_busy()) {
      face_detector_helper_trigger_detection();
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

// #include "cam_helper.h"
// #include "esp32_s3_szp.h"
// #include "esp_camera.h"
// #include "esp_websocket_client.h"
// #include "gs_nav.h"
// #include "mdns.h"
// #include "my_theme.h"
// #include "prov_qr.h"
// #include "sntp_helper.h"
// #include "wifi_prov.h"
// #include <esp_wifi.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/event_groups.h>
// #include <freertos/task.h>
// #include <stdio.h>

// #include "s3_helper.h"

// #include "button_gpio.h"
// #include "iot_button.h"

// #include "img_stack.h"

// #define OTA_ENRTY_BUTTON_GPIO GPIO_NUM_0
// static button_handle_t btn;
// static bool s_ota_mode_active = false;
// extern EventGroupHandle_t wifi_event_group;
// extern const int WIFI_CONNECTED_EVENT;

// typedef enum {
//   BOOT_MODE_NORMAL = 0,
//   BOOT_MODE_OTA,
// } boot_mode_t;

// static boot_mode_t s_boot_mode = BOOT_MODE_NORMAL;

// static const char *TAG = "MAIN";

// extern const gs_page_desc_t page_ota;

// static bool s_ota_triggered = false;
// static void btn_long_cb(void *arg, void *usr_data) {
//   s_boot_mode = BOOT_MODE_OTA;
// }

// static void ota_button_init(void) {
//   const button_config_t btn_cfg = {0};
//   const button_gpio_config_t btn_gpio_cfg = {
//       .gpio_num = OTA_ENRTY_BUTTON_GPIO,
//       .active_level = 0,
//       .enable_power_save = false,
//   };

//   esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &btn);
//   if (ret != ESP_OK) {
//     ESP_LOGE(TAG, "Failed to create button");
//     return;
//   }

//   iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL,
//                          (button_cb_t)btn_long_cb, NULL);
// }

// static boot_mode_t detect_boot_mode(void) {
//   s_boot_mode = BOOT_MODE_NORMAL;

//   ota_button_init();

//   for (int i = 0; i < 500; i++) {
//     if (s_boot_mode == BOOT_MODE_OTA) {
//       break;
//     }
//     vTaskDelay(pdMS_TO_TICKS(10));
//   }
//   iot_button_delete(btn);

//   return s_boot_mode;
// }

// static lv_obj_t *splash_page(lv_obj_t *parent, void *ctx) {
//   lv_obj_t *cont = lv_obj_create(parent);
//   lv_obj_set_size(cont, 320, 240);
//   lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
//   lv_obj_set_style_border_width(cont, 0, 0);

//   lv_obj_t *spinner = lv_spinner_create(cont);
//   lv_spinner_set_anim_params(spinner, 4000, 200);
//   lv_obj_set_size(spinner, 60, 60);
//   lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);
//   lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);
//   lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
//   lv_obj_set_style_arc_color(spinner, lv_color_hex(0x2195f6),
//                              LV_PART_INDICATOR);

//   lv_obj_t *label = lv_label_create(cont);
//   lv_label_set_text(label, "初始化中...");
//   lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
//   lv_obj_align(label, LV_ALIGN_CENTER, 0, 40);

//   return cont;
// }

// const gs_page_desc_t page_splash = {
//     .init_cb = NULL, .render_cb = splash_page, .deinit_cb = NULL};

// void gui_flsuh_task(void *param) {
//   while (1) {
//     if (lvgl_port_lock(-1)) {
//       uint32_t sleep_ms = lv_timer_handler();
//       gs_nav_loop();
//       lvgl_port_unlock();
//       vTaskDelay(pdMS_TO_TICKS(sleep_ms <= 0 ? 1 : sleep_ms));
//     } else {
//       vTaskDelay(pdMS_TO_TICKS(10));
//     }
//   }
// }

// void init_mdns(const char *hostname) {
//   // 初始化 mDNS 组件
//   esp_err_t err = mdns_init();
//   if (err != ESP_OK) {
//     ESP_LOGE(TAG, "mDNS Init failed: %d", err);
//     return;
//   }

//   ESP_ERROR_CHECK(mdns_hostname_set(hostname));
//   ESP_LOGI(TAG, "mDNS hostname set to: [%s.local]", hostname);
//   ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-S3 Iot"));
//   mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

//   mdns_service_txt_item_set("_http", "_tcp", "version", "1.0.0");
// }

// void query_mdns_host(const char *host_name) {
//   ESP_LOGI(TAG, "Querying for [%s.local]...", host_name);

//   struct esp_ip4_addr addr;
//   addr.addr = 0;

//   esp_err_t err = mdns_query_a(host_name, 2000, &addr);
//   if (err == ESP_OK) {
//     ESP_LOGI(TAG, "Found host! IP: " IPSTR, IP2STR(&addr));
//   } else {
//     ESP_LOGE(TAG, "Host not found or Query failed.");
//   }
// }

// #include "esp_bt.h"
// #include "nvs.h"
// #include "nvs_flash.h"
// #include "nvs_helper.h"

// extern const gs_page_desc_t page_cam;

// #include "http_client_helper.h"

// #include "time_test_helper.h"

// void app_main(void) {
//   bsp_i2c_init();
//   pca9557_init();
//   bsp_lvgl_start();
//   my_ui_theme_init();
//   bsp_littlefs_mount();

//   lv_obj_t *container = lv_scr_act();
//   gs_nav_init(container);

//   xTaskCreate(gui_flsuh_task, "gui", 12 * 1024, NULL, 4, NULL);
//   gs_nav_push(&page_splash, NULL);
//   boot_mode_t mode = detect_boot_mode();

//   if (mode == BOOT_MODE_OTA) {
//     ESP_LOGI(TAG, "Booting into OTA mode");
//     gs_nav_pop();
//     gs_nav_push(&page_ota, NULL);
//     return;
//   } else {
//     esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

//     ESP_LOGI(TAG, "Booting into NORMAL mode");
//     // bsp_codec_init();

//     wifi_prov_init();

//     xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true,
//                         portMAX_DELAY);
//     sntp_helper_init();
//     sntp_helper_set_timezone("CST-8");
//     sntp_helper_time("ntp.aliyun.com", 5000);

//     init_mdns("esp32-s3");
//     vTaskDelay(pdMS_TO_TICKS(1000));
//     query_mdns_host("aobara-pc");

//     bsp_camera_init();

//     // 启动s3服务
//     uploader_task_start();

//     http_helper_init();

//     // 初始化图片上传调用互斥量
//     img_stack_init();

//     if (lvgl_port_lock(-1)) {
//       extern const gs_page_desc_t page_main;
//       gs_nav_pop();
//       gs_nav_push_async(&page_main, NULL);
//       lvgl_port_unlock();
//     }
//   }
// }