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
  lv_label_set_text(label, "System Initializing...");
  lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
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

static lv_obj_t *img_obj = NULL;
static QueueHandle_t xQueueFrame = NULL;

// 定义 JPEG 传输结构体
typedef struct {
  uint8_t *jpg_buf;
  size_t jpg_len;
} jpeg_frame_t;

// 16字节对齐分配函数宏
#define ALIGN_16(val) ((val + 15) & ~15)

void task_process_camera(void *pvParameters) {
  // 重新配置：关闭内部任务模式，增加稳定性
  jpeg_enc_config_t config = {
      .width = 320,
      .height = 240,
      .src_type = JPEG_PIXEL_FORMAT_RGB565_LE,
      .subsampling = JPEG_SUBSAMPLE_420,
      .quality = 60,
      .task_enable = false, // 设为 false 以减少内存管理器压力和潜在竞态
  };

  jpeg_enc_handle_t jpeg_enc = NULL;
  jpeg_error_t ret = jpeg_enc_open(&config, &jpeg_enc);
  if (ret != JPEG_ERR_OK) {
    ESP_LOGE(TAG, "Failed to open jpeg encoder, error: %d", ret);
    vTaskDelete(NULL);
  }

  while (1) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      // 确保输出缓冲区至少与原始数据一样大（最坏打算）
      // 原始大小 = $320 \times 240 \times 2 = 153600$ 字节
      int out_buf_size = 153600;
      uint8_t *out_buf =
          heap_caps_aligned_alloc(16, out_buf_size, MALLOC_CAP_SPIRAM);

      if (out_buf) {
        int jpg_size = 0;
        // 增加返回值检查
        ret = jpeg_enc_process(jpeg_enc, fb->buf, fb->len, out_buf,
                               out_buf_size, &jpg_size);

        if (ret == JPEG_ERR_OK) {
          jpeg_frame_t *q_frame = malloc(sizeof(jpeg_frame_t));
          if (q_frame) {
            q_frame->jpg_buf = out_buf;
            q_frame->jpg_len = jpg_size;
            if (xQueueSend(xQueueFrame, &q_frame, 0) != pdPASS) {
              free(out_buf);
              free(q_frame);
            }
          } else {
            free(out_buf);
          }
        } else {
          ESP_LOGE(TAG, "JPEG encoding failed: %d", ret);
          free(out_buf);
        }
      }
      esp_camera_fb_return(fb);
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  jpeg_enc_close(jpeg_enc);
}

void task_process_lcd(void *pvParameters) {
  jpeg_frame_t *frame = NULL;

  while (1) {
    if (xQueueReceive(xQueueFrame, &frame, portMAX_DELAY) == pdPASS) {
      if (frame && frame->jpg_buf) {
        // LVGL v9 图像描述符配置
        static lv_image_dsc_t img_dsc;
        img_dsc.header.cf = LV_COLOR_FORMAT_RAW; // Tiny JPEG 必须使用 RAW 格式
        img_dsc.header.w = 320;
        img_dsc.header.h = 240;
        img_dsc.data_size = frame->jpg_len;
        img_dsc.data = frame->jpg_buf;

        if (lvgl_port_lock(-1)) {
          if (img_obj == NULL) {
            img_obj = lv_image_create(lv_scr_act());
            lv_obj_center(img_obj);
          }
          lv_image_set_src(img_obj,
                           &img_dsc); // LVGL内部会自动调用Tiny JPEG解码
          lvgl_port_unlock();
        }

        free(frame->jpg_buf); // 释放对齐分配的JPEG内存
        free(frame);          // 释放结构体
      }
    }
  }
}

#include "esp_jpeg_dec.h"
#include "esp_jpeg_enc.h"

// 假设屏幕分辨率
#define IMG_W 320
#define IMG_H 240

void capture_encode_decode_display() {
  // --- 0. 变量定义 ---
  camera_fb_t *fb = NULL;
  jpeg_enc_handle_t jpeg_enc = NULL;
  jpeg_dec_handle_t jpeg_dec = NULL;
  uint8_t *jpg_buf = NULL;
  uint8_t *decoded_raw = NULL;
  jpeg_dec_io_t *jpeg_io = NULL;
  jpeg_dec_header_info_t *out_info = NULL;
  jpeg_error_t ret;

  // --- 1. 获取摄像头原始数据 (RGB565) ---
  fb = esp_camera_fb_get();
  if (!fb) {
    ESP_LOGE(TAG, "Camera capture failed");
    return;
  }

  // --- 2. 硬件编码阶段 (RGB565 -> JPEG) ---
  jpeg_enc_config_t enc_config = DEFAULT_JPEG_ENC_CONFIG();
  enc_config.width = fb->width;
  enc_config.height = fb->height;
  enc_config.src_type = JPEG_PIXEL_FORMAT_RGB565_LE; // 匹配摄像头
  enc_config.quality = 80;
  enc_config.task_enable = false; // 线性同步模式

  // 分配对齐的编码输出缓冲区 (64KB足够320x240)
  int jpg_max_size = 64 * 1024;
  jpg_buf = (uint8_t *)jpeg_calloc_align(1, jpg_max_size); // 16字节对齐
  int jpg_real_len = 0;

  if (jpeg_enc_open(&enc_config, &jpeg_enc) == JPEG_ERR_OK) {
    ret = jpeg_enc_process(jpeg_enc, fb->buf, fb->len, jpg_buf, jpg_max_size,
                           &jpg_real_len);
    jpeg_enc_close(jpeg_enc);
    if (ret != JPEG_ERR_OK) {
      ESP_LOGE(TAG, "Encoder process failed: %d", ret);
      goto _cleanup;
    }
  } else {
    ESP_LOGE(TAG, "Encoder open failed");
    goto _cleanup;
  }

  // --- 3. 硬件解码阶段 (JPEG -> RGB565) ---
  jpeg_dec_config_t dec_config = DEFAULT_JPEG_DEC_CONFIG();
  dec_config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE; // 解压回屏幕识别格式

  jpeg_io = (jpeg_dec_io_t *)calloc(1, sizeof(jpeg_dec_io_t));
  out_info =
      (jpeg_dec_header_info_t *)calloc(1, sizeof(jpeg_dec_header_info_t));

  if (jpeg_dec_open(&dec_config, &jpeg_dec) == JPEG_ERR_OK) {
    jpeg_io->inbuf = jpg_buf;
    jpeg_io->inbuf_len = jpg_real_len;

    // 解析 JPEG 头
    if (jpeg_dec_parse_header(jpeg_dec, jpeg_io, out_info) == JPEG_ERR_OK) {
      // 计算并分配解码输出缓冲区
      int decoded_size = out_info->width * out_info->height * 2;
      decoded_raw = (uint8_t *)jpeg_calloc_align(1, decoded_size); // 16字节对齐

      if (decoded_raw) {
        jpeg_io->outbuf = decoded_raw;
        jpeg_io->out_size =
            decoded_size; // 关键：必须赋值，否则汇编加速会引发空指针错误

        ret = jpeg_dec_process(jpeg_dec, jpeg_io); // 执行解码
        if (ret == JPEG_ERR_OK) {
          // --- 4. 调用 BSP 函数显示 ---
          // 修正点：如果 decoded_raw 已在 PSRAM，可考虑直接传给 draw_bitmap
          lcd_draw_pictrue(0, 0, out_info->width, out_info->height,
                           decoded_raw);
        } else {
          ESP_LOGE(TAG, "Decoder process failed: %d", ret);
        }
      }
    }
    jpeg_dec_close(jpeg_dec);
  }

_cleanup:
  // --- 5. 资源回收 (严格按照申请顺序逆序释放) ---
  if (decoded_raw)
    jpeg_free_align(decoded_raw);
  if (jpg_buf)
    jpeg_free_align(jpg_buf);
  if (jpeg_io)
    free(jpeg_io);
  if (out_info)
    free(out_info);
  if (fb)
    esp_camera_fb_return(fb); // 必须归还，否则摄像头驱动会溢出
}

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

  /* ⚠ 注意：
     rgb565_buf 不能立刻释放！
     LVGL正在使用它作为图像数据
  */

  if (fb)
    esp_camera_fb_return(fb);
}

void app_main(void) {
  bsp_i2c_init();
  pca9557_init();
  bsp_lvgl_start();
  bsp_littlefs_mount();
  bsp_codec_init();
  bsp_camera_init();

  // xQueueFrame = xQueueCreate(2, sizeof(jpeg_frame_t *));

  // xTaskCreatePinnedToCore(task_process_camera, "CamTask", 8 * 1024, NULL, 5,
  //                         NULL, 1);
  // xTaskCreatePinnedToCore(task_process_lcd, "LcdTask", 8 * 1024, NULL, 5,
  // NULL,
  //                         0);

  vTaskDelay(pdMS_TO_TICKS(2000));
  jpeg_encode_decode_once();
}