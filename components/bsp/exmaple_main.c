#include "assert.h"
#include "bsp_board.h"
#include "bsp_camera.h"
#include "bsp_config.h"
#include "bsp_lcd.h"
#include "esp_camera.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdint.h>
#include <string.h>

static const char *TAG = "MAIN";

/* QVGA 320x240 RGB565 */
#define CAM_WIDTH 320
#define CAM_HEIGHT 240
#define CAM_BUF_SIZE (CAM_WIDTH * CAM_HEIGHT * 2)

static uint8_t *s_cam_buf = NULL;
static lv_image_dsc_t s_cam_img_dsc;
static lv_obj_t *s_cam_img_obj = NULL;

/* ---- 创建 LVGL 摄像头预览 ---- */
static void camera_view_create(void) {
  s_cam_buf =
      heap_caps_malloc(CAM_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  assert(s_cam_buf);

  s_cam_img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  s_cam_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  s_cam_img_dsc.header.w = CAM_WIDTH;
  s_cam_img_dsc.header.h = CAM_HEIGHT;
  s_cam_img_dsc.data_size = CAM_BUF_SIZE;
  s_cam_img_dsc.data = s_cam_buf;

  s_cam_img_obj = lv_image_create(lv_screen_active());
  lv_image_set_src(s_cam_img_obj, &s_cam_img_dsc);
  lv_obj_center(s_cam_img_obj);

  ESP_LOGI(TAG, "Camera preview created (%dx%d)", CAM_WIDTH, CAM_HEIGHT);
}

/* ---- 摄像头帧抓取任务 ---- */
static void camera_task(void *arg) {
  uint32_t frame_count = 0;

  while (1) {
    camera_fb_t *fb = bsp_camera_get_frame();
    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    memcpy(s_cam_buf, fb->buf, fb->len);
    bsp_camera_return_frame(fb);

    lvgl_port_lock(0);
    lv_obj_invalidate(s_cam_img_obj);
    lvgl_port_unlock();

    if (++frame_count % 100 == 0) {
      ESP_LOGI(TAG, "Frames: %lu", frame_count);
    }

    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

/* ---- 主入口 ---- */
void app_main(void) {
  esp_lcd_panel_handle_t lcd;
  esp_lcd_touch_handle_t touch;

  /* 1. 板级初始化 */
  ESP_ERROR_CHECK(bsp_board_init());

  /* 2. LCD: 上电 → 初始化 → LVGL */
  ESP_ERROR_CHECK(bsp_lcd_power_up());
  ESP_ERROR_CHECK(bsp_lcd_init(&lcd, &touch));
  ESP_ERROR_CHECK(bsp_lvgl_init(lcd, touch));

  /* 3. Camera: 上电 → 初始化 */
  ESP_ERROR_CHECK(bsp_camera_power_up());
  esp_err_t cam_ret =
      bsp_camera_init(20 * 1000 * 1000, PIXFORMAT_RGB565, FRAMESIZE_QVGA, 2);

  /* 4. 创建 UI */
  lvgl_port_lock(0);

  if (cam_ret == ESP_OK) {
    camera_view_create();
  } else {
    lv_obj_t *err_label = lv_label_create(lv_screen_active());
    lv_label_set_text(err_label, "Camera Init Failed!");
    lv_obj_align(err_label, LV_ALIGN_CENTER, 0, 0);
  }

  lvgl_port_unlock();

  /* 5. 启动摄像头任务 */
  if (cam_ret == ESP_OK) {
    xTaskCreate(camera_task, "camera", 4096, NULL, 5, NULL);
  }

  ESP_LOGI(TAG, "App ready");
}
