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
#include "gs_nav.h"
#include "mdns.h"
#include "my_theme.h"
#include "prov_qr.h"
#include "sensor.h"
#include "sntp_helper.h"
#include "wifi_prov.h"
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <stdio.h>

#include "s3_helper.h"

#include "audio_helper.h"
#include "power_manager.h"

#include "face_detector_helper.h"

#include "button_gpio.h"
#include "gs_portal.h"
#include "img_queue.h"
#include "iot_button.h"

#include "context.h"

#include "efuse_helper.h"

#include "time_test_helper.h"

#include "esp_lvgl_port.h"

#define OTA_ENRTY_BUTTON_GPIO GPIO_NUM_4

#define CONFIG_LV_USE_FILE_EXPLORER 1

static button_handle_t btn;
static bool s_ota_mode_active = false;
extern EventGroupHandle_t wifi_event_group;
extern const int WIFI_CONNECTED_EVENT;

static TaskHandle_t s_gui_task_handle = NULL;
static StaticTask_t s_gui_task_tcb;
static StackType_t *s_gui_task_stack = NULL;

typedef enum {
  BOOT_MODE_NORMAL = 0,
  BOOT_MODE_OTA,
} boot_mode_t;

static boot_mode_t s_boot_mode = BOOT_MODE_NORMAL;

static const char *TAG = "MAIN";

extern const gs_page_desc_t page_ota;

static bool s_ota_triggered = false;
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
    if (s_boot_mode == BOOT_MODE_OTA) {
      break;
    }
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

static volatile bool s_gpio_activity = false;

static void touch_indev_event_cb(lv_event_t *e) {
  power_manager_report_activity();
}

void gui_flsuh_task(void *param) {
  while (1) {
    if (lvgl_port_lock(-1)) {
      uint32_t sleep_ms = lv_timer_handler();
      gs_nav_loop();

      if (s_gpio_activity) {
        s_gpio_activity = false;
        power_manager_report_activity();
      }

      lv_indev_t *indev = lv_indev_get_act();
      if (indev && lv_indev_get_state(indev) == LV_INDEV_STATE_PR) {
        power_manager_report_activity();
      }

      lvgl_port_unlock();
      vTaskDelay(pdMS_TO_TICKS(sleep_ms <= 0 ? 10 : sleep_ms));
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

static void IRAM_ATTR gpio_activity_isr_handler(void *arg) {
  s_gpio_activity = true;
}

static void gpio_activity_init(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = BIT64(GPIO_NUM_4),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };
  gpio_config(&io_conf);
  gpio_install_isr_service(0);
  gpio_isr_handler_add(GPIO_NUM_4, gpio_activity_isr_handler, NULL);
  ESP_LOGI(TAG, "GPIO4 activity ISR installed");
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

#include "aes_crypto_helper.h"

esp_err_t generate_device_key(void) {
  uint8_t uuid[16];
  esp_err_t err =
      efuse_helper_read_uuid(uuid); // 从虚拟/真实 eFuse 读取 128 位 UUID

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read UUID from eFuse");
    return err;
  }

  // ESP_LOG_BUFFER_HEX("UUID", uuid, 16);
  uint8_t device_key[16];
  // 使用 HKDF 派生
  err = derive_session_key(uuid, 16, NULL, 0,
                           (const uint8_t *)"guardian-dev-key-v1", 20,
                           device_key);

  ESP_LOG_BUFFER_HEX(" device_key", device_key, 16);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to derive device key");
    return err;
  }

  // 存入 NVS
  err = nvs_helper_set_device_key(device_key);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Device key derived and stored to NVS");
  }
  return err;
}

#include "esp_bt.h"
#include "http_client_helper.h"
#include "monitor_mamager.h"
#include "net_adapter.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_helper.h"
#include "protocol.h"
#include "time_test_helper.h"

#include "task_manager.h"

#include "chat_service.h"

#include "protocol.h"

void debug_init_nvs_value() {
  nvs_helper_set_i32("storage", "device_id", 3);
  nvs_helper_set_i32("storage", "parent_id", 1);
}

void global_socket_init(void) {
  int32_t did = -1, pid = -1;
  nvs_helper_get_i32("storage", "device_id", &did);
  nvs_helper_get_i32("storage", "parent_id", &pid);

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

  // 初始化聊天服务
  chat_service_init();

  // 初始化网络适配器
  net_adapter_init(&global_cfg);

  chat_service_register_reasoning_cb(on_reasoning_received);
  chat_service_register_notify_cb(on_notify_received);

  gs_toast_show("全局Socket连接已启动", GS_TOAST_SUCCESS);

  task_manager_init(did);
}

void gui_task_regsiter() {
  const size_t gui_stack_size = 10 * 1024; // 栈大小（字节）
  s_gui_task_stack = (StackType_t *)heap_caps_malloc(
      gui_stack_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_gui_task_stack) {
    size_t stack_depth = gui_stack_size / sizeof(StackType_t);
    s_gui_task_handle =
        xTaskCreateStatic(gui_flsuh_task, "gui", stack_depth, NULL,
                          4, // 优先级
                          s_gui_task_stack, &s_gui_task_tcb);
    if (s_gui_task_handle) {
      ESP_LOGI("MAIN", "GUI task created on PSRAM stack (%zu bytes)",
               gui_stack_size);
    } else {
      ESP_LOGW("MAIN",
               "xTaskCreateStatic failed, fallback to dynamic allocation");
      heap_caps_free(s_gui_task_stack);
      s_gui_task_stack = NULL;
      xTaskCreate(gui_flsuh_task, "gui", gui_stack_size, NULL, 4, NULL);
    }
  } else {
    ESP_LOGW("MAIN",
             "PSRAM allocation failed for GUI stack, using internal RAM");
    xTaskCreate(gui_flsuh_task, "gui", gui_stack_size, NULL, 4, NULL);
  }

  TEST_MEM_INFO(TAG);
}

void efuse_init() {
  // 在生产模式中请自行删除宏定义部分（请注意实际的efuse为一次性硬件，修改前请确定你在做什么）
#ifdef CONFIG_EFUSE_VIRTUAL
  const uint8_t test_uuid[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC,
                                 0xDD, 0xEE, 0xFF, 0x00};
  esp_err_t ret = efuse_helper_write_test_uuid(test_uuid);
#endif
  generate_device_key();
}

#include "nvs_helper.h"
extern const gs_page_desc_t page_fd;
void app_main(void) {
  if (power_manager_is_deep_sleep_wakeup()) {
    ESP_LOGI(TAG, "=== Resuming from ULP deep sleep ===");
  }

  bsp_board_init();
  esp_lcd_panel_handle_t lcd_panel;
  esp_lcd_touch_handle_t touch_handle;
  bsp_lcd_power_up();
  bsp_lcd_init(&lcd_panel, &touch_handle);
  bsp_lvgl_init(lcd_panel, touch_handle);
  my_ui_theme_init();
  ESP_ERROR_CHECK(bsp_fs_init());

  nvs_helper_init();
  efuse_init();

  debug_print_task_watermarks();
  debug_start_heap_monitor(30000);

  lv_obj_t *container = lv_scr_act();
  gs_nav_init(container);

  gui_task_regsiter();
  gs_nav_push(&page_splash, NULL);
  boot_mode_t mode = detect_boot_mode();

  if (mode == BOOT_MODE_OTA) {
    ESP_LOGI(TAG, "Booting into OTA mode");
    gs_nav_pop();
    gs_nav_push(&page_ota, NULL);
    return;
  } else {
    gpio_activity_init();
    power_manager_init();

    lv_indev_t *touch_indev = bsp_lcd_get_touch_indev();
    if (touch_indev) {
      lv_indev_add_event_cb(touch_indev, touch_indev_event_cb,
                            LV_EVENT_PRESSING, NULL);
      ESP_LOGI(TAG, "Touch indev activity callback registered");
    }

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    ESP_LOGI(TAG, "Booting into NORMAL mode");
    printf("internal=%d\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    printf("dma=%d\n", heap_caps_get_free_size(MALLOC_CAP_DMA));
    wifi_prov_init();

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true,
                        portMAX_DELAY);
    gs_toast_show("联网成功", GS_TOAST_SUCCESS);
    sntp_helper_init();
    sntp_helper_set_timezone("CST-8");
    sntp_helper_time("ntp.aliyun.com", 5000);

    // debug point
    if (IS_DEBUG_MODE) {
      debug_init_nvs_value();
      test_nvs_info();
      TEST_MEM_INFO("TEST");
    }

    init_mdns("esp32-s3");
    vTaskDelay(pdMS_TO_TICKS(1000));
    query_mdns_host("aobara-pc");
    if (!http_ping_server()) {
      ESP_LOGE(TAG, "HTTP ping failed");
      // 这里可以处理错误，但不要直接 abort
    }
    cam_helper_config_t cam_cfg = {
        .xclk_freq_hz = 20000000,         // 10MHz
        .pixel_format = PIXFORMAT_RGB565, // 或者根据你人脸检测需要的格式定义
        .frame_size = FRAMESIZE_QVGA,     // 320x240
        .fb_count = 2,
        .auto_standby_ms = 5000, // 5秒内没有任何 Task 使用自动切断电源休眠
    };
    ESP_ERROR_CHECK(cam_helper_init(&cam_cfg));

    face_detector_helper_init(320, 240);

    // 启动s3服务
    uploader_task_start();

    http_helper_init();
    // 初始化全局socket 流
    global_socket_init();
    // 初始化图片上传调用互斥量
    img_queue_init();

    monitor_task_start();

    // if (IS_DEBUG_MODE) {
    //   test_task_monitor();
    // }

    extern const gs_page_desc_t page_main;

    gs_nav_pop();
    gs_nav_push(&page_main, NULL);
  }
}