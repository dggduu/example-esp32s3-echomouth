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

static void global_service_init() {

  lv_obj_t *container = lv_scr_act();
  gs_nav_init(container);
  xTaskCreate(gui_flsuh_task, "gui", 8196, NULL, 4, NULL);
  // 加一个 Splash
  gs_nav_push(&page_splash, NULL);

  wifi_prov_init();

  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true,
                      portMAX_DELAY);
  sntp_helper_init();
  sntp_helper_set_timezone("CST-8");
  sntp_helper_time("ntp.aliyun.com", 5000);
}

#include "esp_jpeg_dec.h"
#include "esp_jpeg_enc.h"

static void jpeg_encode_decode_once(void) {
  camera_fb_t *fb = NULL;
  jpeg_enc_handle_t enc = NULL;
  jpeg_dec_handle_t dec = NULL;
  jpeg_error_t ret;

  uint8_t *jpg_buf = NULL;
  uint8_t *rgb565_buf = NULL;

  jpeg_dec_io_t *jpeg_io = NULL;
  jpeg_dec_header_info_t *header = NULL;

  /* ---------------- 1. 获取摄像头RGB565 ---------------- */
  fb = esp_camera_fb_get();
  if (!fb) {
    ESP_LOGE(TAG, "Camera capture failed");
    return;
  }

  ESP_LOGI(TAG, "Camera frame: %dx%d, len=%d", fb->width, fb->height, fb->len);

  /* ---------------- 2. JPEG编码 ---------------- */
  jpeg_enc_config_t enc_cfg = DEFAULT_JPEG_ENC_CONFIG();
  enc_cfg.width = fb->width;
  enc_cfg.height = fb->height;
  enc_cfg.src_type = JPEG_PIXEL_FORMAT_RGB565_BE;
  enc_cfg.subsampling = JPEG_SUBSAMPLE_420;
  enc_cfg.quality = 80;
  enc_cfg.task_enable = false;

  ret = jpeg_enc_open(&enc_cfg, &enc);
  if (ret != JPEG_ERR_OK) {
    ESP_LOGE(TAG, "jpeg_enc_open failed");
    goto cleanup;
  }

  int jpg_max_size = 80 * 1024;
  int jpg_len = 0;

  jpg_buf = jpeg_calloc_align(jpg_max_size, 16);
  if (!jpg_buf) {
    ESP_LOGE(TAG, "No mem for jpg_buf");
    goto cleanup;
  }

  ret =
      jpeg_enc_process(enc, fb->buf, fb->len, jpg_buf, jpg_max_size, &jpg_len);

  if (ret != JPEG_ERR_OK) {
    ESP_LOGE(TAG, "jpeg_enc_process failed: %d", ret);
    goto cleanup;
  }

  ESP_LOGI(TAG, "JPEG encoded size: %d bytes", jpg_len);

  /* ---------------- 3. 检查 JPEG Magic 头 ---------------- */
  if (jpg_len >= 2) {
    ESP_LOGI(TAG, "JPEG magic: %02X %02X", jpg_buf[0], jpg_buf[1]);

    if (jpg_buf[0] == 0xFF && jpg_buf[1] == 0xD8) {
      ESP_LOGI(TAG, "JPEG header OK (FFD8)");
    } else {
      ESP_LOGE(TAG, "JPEG header INVALID");
      goto cleanup;
    }
  }

  jpeg_enc_close(enc);
  enc = NULL;

  /* ---------------- 4. JPEG解码 ---------------- */
  jpeg_dec_config_t dec_cfg = DEFAULT_JPEG_DEC_CONFIG();
  dec_cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;

  ret = jpeg_dec_open(&dec_cfg, &dec);
  if (ret != JPEG_ERR_OK) {
    ESP_LOGE(TAG, "jpeg_dec_open failed");
    goto cleanup;
  }

  jpeg_io = calloc(1, sizeof(jpeg_dec_io_t));
  header = calloc(1, sizeof(jpeg_dec_header_info_t));

  jpeg_io->inbuf = jpg_buf;
  jpeg_io->inbuf_len = jpg_len;

  ret = jpeg_dec_parse_header(dec, jpeg_io, header);
  if (ret != JPEG_ERR_OK) {
    ESP_LOGE(TAG, "jpeg_dec_parse_header failed");
    goto cleanup;
  }

  int rgb_size = header->width * header->height * 2;

  rgb565_buf = jpeg_calloc_align(rgb_size, 16);
  if (!rgb565_buf) {
    ESP_LOGE(TAG, "No mem for rgb565_buf");
    goto cleanup;
  }

  jpeg_io->outbuf = rgb565_buf;
  jpeg_io->out_size = rgb_size;

  ret = jpeg_dec_process(dec, jpeg_io);
  if (ret != JPEG_ERR_OK) {
    ESP_LOGE(TAG, "jpeg_dec_process failed");
    goto cleanup;
  }

  ESP_LOGI(TAG, "JPEG decode success: %dx%d", header->width, header->height);

  jpeg_dec_close(dec);
  dec = NULL;

  /* ---------------- 5. LVGL 显示 ---------------- */
  static lv_image_dsc_t img_dsc;

  img_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  img_dsc.header.w = header->width;
  img_dsc.header.h = header->height;
  img_dsc.data_size = rgb_size;
  img_dsc.data = rgb565_buf;

  lv_obj_t *img = lv_image_create(lv_scr_act());
  lv_image_set_src(img, &img_dsc);
  lv_obj_center(img);

  ESP_LOGI(TAG, "Image displayed");

cleanup:

  if (enc)
    jpeg_enc_close(enc);
  if (dec)
    jpeg_dec_close(dec);

  if (jpeg_io)
    free(jpeg_io);
  if (header)
    free(header);

  if (jpg_buf)
    jpeg_free_align(jpg_buf);

  if (fb)
    esp_camera_fb_return(fb);
}

extern const gs_page_desc_t page_cam;

void app_main(void) {
  bsp_i2c_init();
  pca9557_init();
  bsp_lvgl_start();
  bsp_littlefs_mount();
  bsp_codec_init();
  bsp_camera_init();

  // jpeg_encode_decode_once();

  my_ui_theme_init();

  lv_obj_t *container = lv_scr_act();
  gs_nav_init(container);
  xTaskCreate(gui_flsuh_task, "gui", 1024 * 16, NULL, 4, NULL);

  if (lvgl_port_lock(-1)) {
    extern const gs_page_desc_t page_main;
    gs_nav_pop();
    gs_nav_push_async(&page_cam, NULL);
    lvgl_port_unlock();
  }
  // 全局初始化
  // global_service_init();

  // vTaskDelay(pdMS_TO_TICKS(2000));
}