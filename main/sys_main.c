#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"

#include "esp_bt.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

// BSP 与 硬件头文件
#include "StyleSheet.h"
#include "bsp_audio.h"
#include "bsp_board.h"
#include "bsp_camera.h"
#include "bsp_config.h"
#include "bsp_fs.h"
#include "bsp_lcd.h"
#include "bsp_pca9539.h"
#include "button_gpio.h"
#include "iot_button.h"
#include "power_manager.h"

// 业务与 Helper 头文件
#include "aes_crypto_helper.h"
#include "audio_helper.h"
#include "cam_helper.h"
#include "chat_service.h"
#include "context.h"
#include "efuse_helper.h"
#include "face_detector_helper.h"
#include "gs_nav.h"
#include "gs_portal.h"
#include "http_client_helper.h"
#include "img_queue.h"
#include "mdns_helper.h"
#include "monitor_mamager.h"
#include "my_theme.h"
#include "net_adapter.h"
#include "nvs_helper.h"
#include "protocol.h"
#include "prov_qr.h"
#include "s3_helper.h"
#include "sensor.h"
#include "sntp_helper.h"
#include "task_manager.h"
#include "time_test_helper.h"
#include "wifi_prov.h"

#define OTA_ENTRY_BUTTON_GPIO GPIO_NUM_4
#define TAG "MAIN"

static button_handle_t s_ota_btn = NULL;

extern EventGroupHandle_t wifi_event_group;
extern const int WIFI_CONNECTED_EVENT;
extern const gs_page_desc_t page_ota;
extern const gs_page_desc_t page_main;

typedef enum {
  BOOT_MODE_NORMAL = 0,
  BOOT_MODE_OTA,
} boot_mode_t;

static boot_mode_t s_boot_mode = BOOT_MODE_NORMAL;

/*---------------------------------------------------------------
 * 按键与启动模式检测
 *--------------------------------------------------------------*/
static void ota_btn_long_press_cb(void *arg, void *usr_data) {
  s_boot_mode = BOOT_MODE_OTA;
  ESP_LOGW(TAG, "OTA long press detected, changing boot mode to OTA!");
}

static boot_mode_t detect_boot_mode(void) {
  s_boot_mode = BOOT_MODE_NORMAL;

  const button_config_t btn_cfg = {0};
  const button_gpio_config_t btn_gpio_cfg = {
      .gpio_num = OTA_ENTRY_BUTTON_GPIO,
      .active_level = 0,
      .enable_power_save = false,
      .disable_pull = 0,
  };

  esp_err_t ret =
      iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &s_ota_btn);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create OTA detection button");
    return BOOT_MODE_NORMAL;
  }

  iot_button_register_cb(s_ota_btn, BUTTON_PRESS_DOWN, NULL,
                         (button_cb_t)ota_btn_long_press_cb, NULL);

  // 检测长按（等待 1 秒）
  for (int i = 0; i < 100; i++) {
    if (s_boot_mode == BOOT_MODE_OTA)
      break;
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  iot_button_delete(s_ota_btn);
  s_ota_btn = NULL;
  return s_boot_mode;
}

/*---------------------------------------------------------------
 * UI 启动 Splash 页面
 *--------------------------------------------------------------*/
static lv_obj_t *splash_page(lv_obj_t *parent, void *ctx) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *spinner = lv_spinner_create(cont);
  lv_spinner_set_anim_params(spinner, 4000, 200);
  lv_obj_set_size(spinner, 60, 60);
  lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner, S_COLOR_PRIMARY, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner, S_COLOR_PRIMARY_CONTAINER, LV_PART_MAIN);

  lv_obj_t *label = lv_label_create(cont);
  lv_obj_t *label_version = lv_label_create(cont);

  lv_label_set_text(label, "正在初始化...");
  lv_label_set_text(label_version, "ver: 0.1.0");

  lv_obj_set_style_text_color(label, S_TEXT_SECONDARY, 0);
  lv_obj_set_style_text_color(label_version, S_COLOR_PRIMARY, 0);

  return cont;
}

const gs_page_desc_t page_splash = {
    .init_cb = NULL, .render_cb = splash_page, .deinit_cb = NULL};

/*---------------------------------------------------------------
 * 回调与事件处理
 *--------------------------------------------------------------*/
static void toast_async_cb(void *arg) {
  char *text = (char *)arg;
  if (text) {
    gs_toast_show(text, GS_TOAST_FAILED);
    free(text);
  }
}

static void show_toast_async(const char *msg) {
  if (msg) {
    lv_async_call(toast_async_cb, strdup(msg));
  }
}

static void on_reasoning_received(const char *msg) { show_toast_async(msg); }

static void on_notify_received(uint32_t msg_id, uint8_t sender,
                               const char *preview) {
  gs_toast_config_t cfg = {
      .msg = "新消息",
      .type = GS_TOAST_INFO,
      .stay_time = 3000,
      .click_cb = NULL,
  };
  gs_portal_toast_show(cfg);
}

static void touch_indev_event_cb(lv_event_t *e) {
  power_manager_report_activity();
}

/* 导航 loop 调度任务 */
static void nav_loop_task(void *param) {
  while (1) {
    if (lvgl_port_lock(pdMS_TO_TICKS(50))) {
      gs_nav_loop();

      lv_indev_t *indev = lv_indev_get_act();
      if (indev && lv_indev_get_state(indev) == LV_INDEV_STATE_PR) {
        power_manager_report_activity();
      }
      lvgl_port_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

/*---------------------------------------------------------------
 * 设备密钥生成与全局网络初始化
 *--------------------------------------------------------------*/
static esp_err_t generate_device_key(void) {
  uint8_t uuid[16];
  esp_err_t err = efuse_helper_read_uuid(uuid);
  if (err != ESP_OK)
    return err;

  uint8_t device_key[16];
  err = derive_session_key(uuid, 16, NULL, 0,
                           (const uint8_t *)"guardian-dev-key-v1", 20,
                           device_key);
  if (err != ESP_OK)
    return err;

  return nvs_helper_set_device_key(device_key);
}

static void global_socket_init(void) {
  int32_t did = -1, pid = -1;
  nvs_helper_get_i32("storage", "device_id", &did);
  nvs_helper_get_i32("storage", "parent_id", &pid);

  ESP_LOGI(TAG, "Binding Status -> Device ID: %ld, Parent ID: %ld", did, pid);
  if (did == -1 || pid == -1) {
    gs_toast_show("无法获取用户绑定状态", GS_TOAST_FAILED);
    return;
  }

  char server_ip[32] = {0};
  // 此处 get_mdns_server_ip 会直接读取同步初始化后缓存好的服务器 IP
  if (!get_mdns_server_ip(server_ip, sizeof(server_ip))) {
    gs_toast_show("无法获取服务器 IP", GS_TOAST_FAILED);
    return;
  }

  static net_config_t global_cfg;
  memset(&global_cfg, 0, sizeof(global_cfg));
  strlcpy(global_cfg.host, server_ip, sizeof(global_cfg.host));
  global_cfg.port = 3000;
  snprintf(global_cfg.user_id, sizeof(global_cfg.user_id), "%ld", (long)pid);
  snprintf(global_cfg.device_id, sizeof(global_cfg.device_id), "%ld",
           (long)did);

  chat_service_init();
  net_adapter_init(&global_cfg);

  chat_service_register_reasoning_cb(on_reasoning_received);
  chat_service_register_notify_cb(on_notify_received);

  gs_toast_show("全局网络已连接", GS_TOAST_SUCCESS);
  task_manager_init(did);
}

static void efuse_init(void) {
#ifdef CONFIG_EFUSE_VIRTUAL
  const uint8_t test_uuid[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC,
                                 0xDD, 0xEE, 0xFF, 0x00};
  efuse_helper_write_test_uuid(test_uuid);
#endif
  generate_device_key();
}

/*---------------------------------------------------------------
 * 主入口 app_main
 *--------------------------------------------------------------*/
esp_lcd_panel_handle_t lcd_panel;
esp_lcd_touch_handle_t touch_handle;

void app_main(void) {
  if (power_manager_is_deep_sleep_wakeup()) {
    ESP_LOGI(TAG, "=== Resuming from ULP / Touch Wakeup ===");
  }

  /* 1. 硬件基础模块初始化 */
  bsp_board_init();
  bsp_lcd_power_up();
  bsp_lcd_init(&lcd_panel, &touch_handle);

  /* 2. UI 与 LVGL 端口初始化 */
  bsp_lvgl_init(lcd_panel, touch_handle);
  if (lvgl_port_lock(pdMS_TO_TICKS(1000))) {
    my_ui_theme_init();
    lvgl_port_unlock();
  }
  ESP_ERROR_CHECK(bsp_fs_init());

  /* 3. NVS & eFuse 密钥管理 */
  nvs_helper_init();
  efuse_init();

  debug_print_task_watermarks();
  debug_start_heap_monitor(30000);

  /* 4. 压入 Splash 启动界面 */
  if (lvgl_port_lock(pdMS_TO_TICKS(1000))) {
    lv_obj_t *container = lv_scr_act();
    gs_nav_init(container);
    gs_nav_push(&page_splash, NULL);
    lvgl_port_unlock();
  }

  /* 5. 启动 GUI 导航维护 Task */
  xTaskCreatePinnedToCore(nav_loop_task, "nav_loop", 3072, NULL, 4, NULL, 1);

  /* 6. BOOT 模式判断 */
  boot_mode_t mode = detect_boot_mode();

  if (mode == BOOT_MODE_OTA) {
    ESP_LOGI(TAG, "Booting into OTA Mode");
    if (lvgl_port_lock(pdMS_TO_TICKS(1000))) {
      gs_nav_pop();
      gs_nav_push(&page_ota, NULL);
      lvgl_port_unlock();
    }
    return; // 进入 OTA 模式后停止执行后续正常业务
  }

  /* 7. 正常模式下的业务与网络流程 */
  ESP_LOGI(TAG, "Booting into NORMAL Mode");
  power_manager_init();

  lv_indev_t *touch_indev = bsp_lcd_get_touch_indev();
  if (touch_indev && lvgl_port_lock(pdMS_TO_TICKS(1000))) {
    lv_indev_add_event_cb(touch_indev, touch_indev_event_cb, LV_EVENT_PRESSING,
                          NULL);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "Touch indev activity callback registered");
  }

  // 7.1 Wi-Fi 配网与连接等待
  wifi_prov_init();
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true,
                      portMAX_DELAY);
  gs_toast_show("联网成功", GS_TOAST_SUCCESS);
  vTaskDelay(pdMS_TO_TICKS(1000));

  // 7.4 摄像头初始化
  ESP_LOGI(TAG, "Step 2/6: Initializing Camera...");
  cam_helper_config_t cam_cfg = {
      .xclk_freq_hz = 20000000,
      .pixel_format = PIXFORMAT_RGB565,
      .frame_size = FRAMESIZE_QVGA,
      .fb_count = 2,
      .auto_standby_ms = 5000,
  };
  ESP_ERROR_CHECK(cam_helper_init(&cam_cfg));

  // 7.5 人脸识别 & 图片队列
  ESP_LOGI(TAG, "Step 3/6: Initializing Face Detector...");
  face_detector_helper_init(320, 240);

  ESP_LOGI(TAG, "Step 4/6: Starting Image Uploader...");
  uploader_task_start();

  // 7.2 时间同步 (SNTP)
  sntp_helper_init();
  sntp_helper_set_timezone("CST-8");
  sntp_helper_time("ntp.aliyun.com", 5000);

  // 7.3 同步初始化 HTTP & mDNS (必须阻塞等待局域网域名/IP解析就绪)
  ESP_LOGI(TAG, "Step 1/6: Initializing HTTP & mDNS...");
  http_helper_init();
  if (!http_ping_server()) {
    ESP_LOGE(TAG, "HTTP ping server failed!");
  }

  // 7.6 全局 WebSocket & Task 监控
  ESP_LOGI(TAG, "Step 5/6: Connecting Global Socket...");
  global_socket_init();
  img_queue_init();

  ESP_LOGI(TAG, "Step 6/6: Starting Monitor Task...");
  monitor_task_start();

  // 8. 所有底层组件初始化完毕，切换至 App 主页面
  ESP_LOGI(TAG, "Switching to Main Page...");
  if (lvgl_port_lock(pdMS_TO_TICKS(3000))) {
    gs_nav_pop();
    gs_nav_push(&page_main, NULL);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "Main page pushed successfully!");
  } else {
    ESP_LOGE(TAG, "Failed to acquire LVGL lock, pushing async");
    gs_nav_push_async(&page_main, NULL);
  }
}