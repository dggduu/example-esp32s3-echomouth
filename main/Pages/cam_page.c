#include "cam_helper.h"
#include "driver/gpio.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gs_nav.h"
#include "img_stack.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

#include "esp_lvgl_port.h"

#define TAG "PAGE_CAM"
#define CAM_BUTTON_GPIO GPIO_NUM_0
#define JPEG_MAX_SIZE (80 * 1024)

static lv_obj_t *img_obj;
static lv_image_dsc_t img_dsc;
static uint8_t *preview_buf = NULL;
static size_t preview_buf_size = 0;
static volatile bool frame_ready = false;

static QueueHandle_t cam_evt_queue = NULL;
static TaskHandle_t cam_fetch_task_handle = NULL;
static TaskHandle_t cam_capture_task_handle = NULL;

/* ============================ */
/* Camera Fetch Task (High Prio)*/
/* ============================ */

static void cam_fetch_task(void *arg) {
  ESP_LOGI(TAG, "Camera fetch task started");
  while (1) {
    // 使用标准驱动接口
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      ESP_LOGW(TAG, "Camera capture failed (timeout)");
      vTaskDelay(pdMS_TO_TICKS(10)); // 超时后稍微等待
      continue;
    }

    // 第一次运行时分配 PSRAM 缓冲区
    if (!preview_buf) {
      preview_buf = heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM);
      if (preview_buf) {

        ESP_LOGI("TAG", "PSRAM buffer allocated: %d bytes", fb->len);

        img_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
        img_dsc.header.w = fb->width;
        img_dsc.header.h = fb->height;
        img_dsc.header.stride = fb->width * 2;
        img_dsc.data_size = fb->len;
        img_dsc.data = preview_buf;
      } else {
        ESP_LOGE(TAG, "PSRAM allocation FAILED!");
      }
    }

    if (preview_buf) {
      memcpy(preview_buf, fb->buf, fb->len);
      frame_ready = true;
      ESP_LOGD(TAG, "Frame copied, frame_ready set");
    }

    if (preview_buf) {
      if (lvgl_port_lock(0)) {
        lv_image_set_src(img_obj, &img_dsc);
        lv_obj_invalidate(img_obj);
        lvgl_port_unlock();
        ESP_LOGI(TAG, "Direct image set done");
      }
    }

    esp_camera_fb_return(fb); // 必须立即释放 fb，否则驱动会 timeout

    // 限制采集频率，减轻 PSRAM 带宽压力
    // 目标 20 FPS: 1000 / 20 = 50ms
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

/* ============================ */
/* Capture Logic                */
/* ============================ */

static void cam_do_capture(void) {
  camera_fb_t *fb = cam_helper_get_fb();
  if (!fb)
    return;

  jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
  cfg.width = fb->width;
  cfg.height = fb->height;
  cfg.src_type = JPEG_PIXEL_FORMAT_RGB565_BE;
  cfg.quality = 80;
  cfg.task_enable = false;

  jpeg_enc_handle_t enc;
  if (jpeg_enc_open(&cfg, &enc) != JPEG_ERR_OK) {
    cam_helper_return_fb(fb);
    return;
  }

  uint8_t *jpg_buf = jpeg_calloc_align(JPEG_MAX_SIZE, 16);
  if (!jpg_buf) {
    jpeg_enc_close(enc);
    cam_helper_return_fb(fb);
    return;
  }

  int out_len = 0;
  if (jpeg_enc_process(enc, fb->buf, fb->len, jpg_buf, JPEG_MAX_SIZE,
                       &out_len) == JPEG_ERR_OK) {
    char path[128];
    sprintf(path, "/littlefs/%llu.jpg", esp_timer_get_time() / 1000ULL);
    FILE *f = fopen(path, "wb");
    if (f) {
      fwrite(jpg_buf, 1, out_len, f);
      fclose(f);
      img_stack_push(path);
      ESP_LOGI(TAG, "Saved: %s (%d bytes)", path, out_len);
    }
  }

  jpeg_free_align(jpg_buf);
  jpeg_enc_close(enc);
  cam_helper_return_fb(fb);
}

static void cam_event_task(void *arg) {
  uint32_t io_num;
  while (1) {
    if (xQueueReceive(cam_evt_queue, &io_num, portMAX_DELAY)) {
      cam_do_capture();
    }
  }
}

/* ============================ */
/* GPIO ISR                     */
/* ============================ */

static void IRAM_ATTR cam_gpio_isr(void *arg) {
  uint32_t gpio_num = (uint32_t)arg;
  xQueueSendFromISR(cam_evt_queue, &gpio_num, NULL);
}

/* ============================ */
/* Page Lifecycle               */
/* ============================ */

static void *page_cam_init(void *args) {
  gpio_config_t io_conf = {.intr_type = GPIO_INTR_NEGEDGE,
                           .mode = GPIO_MODE_INPUT,
                           .pin_bit_mask = 1ULL << CAM_BUTTON_GPIO,
                           .pull_down_en = 0,
                           .pull_up_en = 1};
  gpio_config(&io_conf);

  cam_evt_queue = xQueueCreate(4, sizeof(uint32_t));
  gpio_install_isr_service(0);
  gpio_isr_handler_add(CAM_BUTTON_GPIO, cam_gpio_isr, (void *)CAM_BUTTON_GPIO);

  xTaskCreate(cam_event_task, "cam_cap_task", 1024 * 8, NULL, 5,
              &cam_capture_task_handle); // 拍照任务
  xTaskCreate(cam_fetch_task, "cam_fetch_task", 1024 * 8, NULL, 4,
              &cam_fetch_task_handle); // 高优先级采集任务

  return NULL;
}

static void page_cam_deinit(void *ctx) {
  if (cam_fetch_task_handle)
    vTaskDelete(cam_fetch_task_handle); // 销毁采集任务
  if (cam_capture_task_handle)
    vTaskDelete(cam_capture_task_handle);
  if (cam_evt_queue)
    vQueueDelete(cam_evt_queue);
  gpio_isr_handler_remove(CAM_BUTTON_GPIO);

  if (preview_buf) {
    heap_caps_free(preview_buf); // 释放 PSRAM 缓存
    preview_buf = NULL;
  }
}

static void page_cam_update(void *ctx) {
  // 在 page_cam_update 中
  if (frame_ready && img_obj) {
    ESP_LOGI(TAG, "Updating image, w=%d, h=%d", img_dsc.header.w,
             img_dsc.header.h);
    lv_image_set_src(img_obj, &img_dsc);
    lv_obj_invalidate(img_obj);
    frame_ready = false;
  } else {
    ESP_LOGW(TAG, "Update skipped: ready=%d, img_obj=%p", frame_ready, img_obj);
  }
}

static lv_obj_t *page_cam_render(lv_obj_t *parent, void *ctx) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(cont, lv_color_black(), 0);

  img_obj = lv_image_create(cont);
  lv_obj_center(img_obj);

  return cont;
}

const gs_page_desc_t page_cam = {
    .init_cb = page_cam_init,
    .render_cb = page_cam_render,
    .update_cb = page_cam_update,
    .deinit_cb = page_cam_deinit,
};