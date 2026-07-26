#include "StyleSheet.h"
#include "bsp_audio.h"
#include "bsp_board.h"
#include "bsp_camera.h"
#include "bsp_config.h"
#include "bsp_fs.h"
#include "bsp_lcd.h"
#include "bsp_pca9539.h"
#include "cam_helper.h"
#include "esp_websocket_client.h"
#include "freertos/projdefs.h"
#include "gs_nav.h"
#include "mdns.h"
#include "my_theme.h"
#include "prov_qr.h"
#include "sensor.h"
#include "sntp_helper.h"
#include "wifi_prov.h"
#include "wifi_provisioning/manager.h"
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <stdio.h>

#include "audio_helper.h"
#include "button_gpio.h"
#include "context.h"
#include "efuse_helper.h"
#include "esp_lvgl_port.h"
#include "face_detector_helper.h"
#include "gs_portal.h"
#include "img_queue.h"
#include "iot_button.h"
#include "power_manager.h"
#include "s3_helper.h"
#include "time_test_helper.h"

#define OTA_ENRTY_BUTTON_GPIO GPIO_NUM_4

static button_handle_t btn;
static bool s_ota_mode_active = false;
extern EventGroupHandle_t wifi_event_group;
extern const int WIFI_CONNECTED_EVENT;

typedef enum {
  BOOT_MODE_NORMAL = 0,
  BOOT_MODE_OTA,
} boot_mode_t;

static boot_mode_t s_boot_mode = BOOT_MODE_NORMAL;
static const char *TAG = "MAIN";

extern const gs_page_desc_t page_ota;

static void btn_long_cb(void *arg, void *usr_data) {
  s_boot_mode = BOOT_MODE_OTA;
}

static void ota_button_init(void) {
  const button_config_t btn_cfg = {0};
  const button_gpio_config_t btn_gpio_cfg = {
      .gpio_num = OTA_ENRTY_BUTTON_GPIO,
      .active_level = 0,
      .enable_power_save = false,
      .disable_pull = 0,
  };

  esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &btn);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create button");
    return;
  }

  iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL,
                         (button_cb_t)btn_long_cb, NULL);
}

static boot_mode_t detect_boot_mode(void) {
  s_boot_mode = BOOT_MODE_NORMAL;
  ota_button_init();

  for (int i = 0; i < 100; i++) {
    if (s_boot_mode == BOOT_MODE_OTA)
      break;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  iot_button_delete(btn);
  return s_boot_mode;
}

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
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);
  lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner, S_COLOR_PRIMARY, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner, S_COLOR_PRIMARY_CONTAINER, LV_PART_MAIN);

  lv_obj_t *label = lv_label_create(cont);
  lv_obj_t *label_version = lv_label_create(cont);
  char version[50];
  snprintf(version, sizeof(version), "ver:%s", "0.1.0");
  lv_label_set_text(label, "正在初始化...");
  lv_label_set_text(label_version, version);

  lv_obj_set_style_text_color(label, S_TEXT_SECONDARY, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 40);
  lv_obj_set_style_text_color(label_version, S_COLOR_PRIMARY, 0);
  lv_obj_align(label_version, LV_ALIGN_CENTER, 0, 55);

  return cont;
}

const gs_page_desc_t page_splash = {
    .init_cb = NULL, .render_cb = splash_page, .deinit_cb = NULL};

static void toast_async_cb(void *arg) {
  const char *text = (const char *)arg;
  gs_toast_show(text, GS_TOAST_FAILED);
  free((void *)text);
}

static void show_toast_async(const char *msg) {
  lv_async_call(toast_async_cb, strdup(msg));
}

static void on_reasoning_received(const char *msg) { show_toast_async(msg); }

static const char *notify_msg = "新消息";

static void on_notify_received(uint32_t msg_id, uint8_t sender,
                               const char *preview) {
  gs_toast_config_t cfg = {.msg = notify_msg,
                           .type = GS_TOAST_INFO,
                           .stay_time = 3000,
                           .click_cb = NULL};
  gs_portal_toast_show(cfg);
}

/* 监听触摸按下的 LVGL 事件，用于唤醒/重置电源超时 */
static void touch_indev_event_cb(lv_event_t *e) {
  power_manager_report_activity();
}

static void nav_loop_timer_cb(lv_timer_t *timer) {
  // 🟢 此时已经在 LVGL 主 Task 内部，并且已经持有 LVGL 锁，千万不要再调
  // lvgl_port_lock！
  gs_nav_loop();

  // 检查触摸状态
  lv_indev_t *indev = lv_indev_get_act();
  if (indev && lv_indev_get_state(indev) == LV_INDEV_STATE_PR) {
    power_manager_report_activity();
  }
}

// 在 app_main 初始化 LVGL 之后注册（必须在 lvgl_port_lock 保护下注册一次即可）
void init_nav_loop(void) {
  if (lvgl_port_lock(-1)) {
    // 创建一个 20ms 执行一次的 LVGL 定时器
    lv_timer_create(nav_loop_timer_cb, 20, NULL);
    lvgl_port_unlock();
  }
}

/* 导航/业务逻辑维持任务*/
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

void init_mdns(const char *hostname) {
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mDNS Init failed: %d", err);
    return;
  }
  ESP_ERROR_CHECK(mdns_hostname_set(hostname));
  ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-S3 Iot"));
  mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
  mdns_service_txt_item_set("_http", "_tcp", "version", "1.0.0");
}

void query_mdns_host(const char *host_name) {
  struct esp_ip4_addr addr;
  addr.addr = 0;
  esp_err_t err = mdns_query_a(host_name, 2000, &addr);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Found host! IP: " IPSTR, IP2STR(&addr));
  } else {
    ESP_LOGE(TAG, "Host not found or Query failed.");
  }
}

#include "aes_crypto_helper.h"

esp_err_t generate_device_key(void) {
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

#include "chat_service.h"
#include "esp_bt.h"
#include "http_client_helper.h"
#include "monitor_mamager.h"
#include "net_adapter.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_helper.h"
#include "protocol.h"
#include "task_manager.h"

void debug_init_nvs_value() {
  nvs_handle_t handle;
  if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_i32(handle, "device_id", 2);
    nvs_set_i32(handle, "parent_id", 3);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI("DEBUG", "NVS Force set to DID:2, PID:3");
  }
}

void global_socket_init(void) {
  int32_t did = -1, pid = -1;
  nvs_helper_get_i32("storage", "device_id", &did);
  nvs_helper_get_i32("storage", "parent_id", &pid);
  ESP_LOGI(TAG, "did:%d,pid:%d", did, pid);
  if (did == -1 || pid == -1) {
    gs_toast_show("无法获取用户绑定状态", GS_TOAST_FAILED);
    return;
  }

  char server_ip[32] = {0};
  if (!get_mdns_server_ip(server_ip, sizeof(server_ip))) {
    gs_toast_show("无法获取服务器地址", GS_TOAST_FAILED);
    return;
  }

  static net_config_t global_cfg;
  strncpy(global_cfg.host, server_ip, sizeof(global_cfg.host) - 1);
  global_cfg.port = 3000;
  snprintf(global_cfg.user_id, sizeof(global_cfg.user_id), "%ld", pid);
  snprintf(global_cfg.device_id, sizeof(global_cfg.device_id), "%ld", did);

  chat_service_init();
  net_adapter_init(&global_cfg);

  chat_service_register_reasoning_cb(on_reasoning_received);
  chat_service_register_notify_cb(on_notify_received);

  gs_toast_show("全局Socket连接已启动", GS_TOAST_SUCCESS);
  task_manager_init(did);
}

void efuse_init() {
#ifdef CONFIG_EFUSE_VIRTUAL
  const uint8_t test_uuid[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC,
                                 0xDD, 0xEE, 0xFF, 0x00};
  efuse_helper_write_test_uuid(test_uuid);
#endif
  generate_device_key();
}

extern const gs_page_desc_t page_fd;
esp_lcd_panel_handle_t lcd_panel;
esp_lcd_touch_handle_t touch_handle;
void app_main(void) {
  if (power_manager_is_deep_sleep_wakeup()) {
    ESP_LOGI(TAG, "=== Resuming from ULP I2C touch wakeup ===");
  }

  /* 1. 初始化底层硬件与 LCD */
  bsp_board_init();

  bsp_lcd_power_up();
  bsp_lcd_init(&lcd_panel, &touch_handle);

  /* 2. 初始化 LVGL (内含 esp_lvgl_port_init，它会自动启动后台刷新任务) */
  bsp_lvgl_init(lcd_panel, touch_handle);
  if (lvgl_port_lock(0)) {
    my_ui_theme_init();
    lvgl_port_unlock();
  }
  ESP_ERROR_CHECK(bsp_fs_init());

  nvs_helper_init();
  efuse_init();

  debug_print_task_watermarks();
  debug_start_heap_monitor(30000);

  /* 3. 在锁保护下对 LVGL 进行初始化布局 */
  if (lvgl_port_lock(0)) {
    lv_obj_t *container = lv_scr_act();
    gs_nav_init(container);
    gs_nav_push(&page_splash, NULL);
    lvgl_port_unlock();
  }

  /* 4. 启动辅助导航调度任务 */
  xTaskCreatePinnedToCore(nav_loop_task, "nav_loop", 3072, NULL, 4, NULL, 1);

  boot_mode_t mode = detect_boot_mode();

  if (mode == BOOT_MODE_OTA) {
    ESP_LOGI(TAG, "Booting into OTA mode");
    if (lvgl_port_lock(0)) {
      gs_nav_pop();
      gs_nav_push(&page_ota, NULL);
      lvgl_port_unlock();
    }
    return;
  } else {
    power_manager_init();

    lv_indev_t *touch_indev = bsp_lcd_get_touch_indev();
    if (touch_indev && lvgl_port_lock(0)) {
      lv_indev_add_event_cb(touch_indev, touch_indev_event_cb,
                            LV_EVENT_PRESSING, NULL);
      lvgl_port_unlock();
      ESP_LOGI(TAG, "Touch indev activity callback registered");
    }

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    ESP_LOGI(TAG, "Booting into NORMAL mode");
    wifi_prov_init();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true,
                        portMAX_DELAY);

    // if (lvgl_port_lock(0)) {
    gs_toast_show("联网成功", GS_TOAST_SUCCESS);
    //   lvgl_port_unlock();
    // }

    sntp_helper_init();
    sntp_helper_set_timezone("CST-8");
    sntp_helper_time("ntp.aliyun.com", 5000);

    // 首次配网后重启，避免 wifi_prov 残留状态导致 mdns_init 卡死
    bool provisioned = false;
    wifi_prov_mgr_is_provisioned(&provisioned);
    nvs_handle_t h;
    bool do_restart = false;
    if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
      uint8_t flag = 0;
      nvs_get_u8(h, "post_prov", &flag);
      if (flag == 1) {
        // 已重启过，清除标记，正常启动
        nvs_set_u8(h, "post_prov", 0);
        nvs_commit(h);
      } else if (provisioned) {
        // 刚配完网（首次），设标记后重启
        nvs_set_u8(h, "post_prov", 1);
        nvs_commit(h);
        do_restart = true;
      }
      nvs_close(h);
    }

    if (do_restart) {
      ESP_LOGI(TAG, "Fresh provision done, rebooting for clean init...");
      vTaskDelay(pdMS_TO_TICKS(500));
      esp_restart();
    }

    ESP_LOGI(TAG, "Step 1/6: init mDNS...");
    init_mdns("esp32-s3");
    vTaskDelay(pdMS_TO_TICKS(1000));
    query_mdns_host("aobara-pc");
    http_helper_init();
    if (!http_ping_server()) {
      ESP_LOGE(TAG, "HTTP ping failed");
    }

    ESP_LOGI(TAG, "Step 2/6: HTTP done, init camera...");
    cam_helper_config_t cam_cfg = {
        .xclk_freq_hz = 20000000,
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size = FRAMESIZE_QVGA,
        .fb_count = 2,
        .auto_standby_ms = 5000,
    };
    ESP_ERROR_CHECK(cam_helper_init(&cam_cfg));

    ESP_LOGI(TAG, "Step 3/6: Camera done, init face detector...");
    face_detector_helper_init(320, 240);

    ESP_LOGI(TAG, "Step 4/6: Face detector done, start uploader...");
    uploader_task_start();

    ESP_LOGI(TAG, "Step 5/6: Uploader started, init WebSocket...");
    global_socket_init();
    img_queue_init();

    ESP_LOGI(TAG, "Step 6/6: WebSocket done, start monitor...");
    monitor_task_start();

    extern const gs_page_desc_t page_main;

    // LVGL 锁可能被刷新任务持有，用超时等待而非立即失败
    // 否则 splash 页面永远不消失
    ESP_LOGI(TAG, "Waiting for LVGL lock to push main page...");
    if (lvgl_port_lock(pdMS_TO_TICKS(3000))) {
      gs_nav_pop();
      gs_nav_push(&page_main, NULL);
      lvgl_port_unlock();
      ESP_LOGI(TAG, "Main page pushed successfully");
    } else {
      ESP_LOGE(TAG, "Failed to acquire LVGL lock, pushing async");
      gs_nav_push_async(&page_main, NULL);
    }
  }
}
