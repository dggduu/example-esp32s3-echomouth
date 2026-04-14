#include "button_gpio.h"
#include "cam_shared.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "gs_nav.h"
#include "gs_portal.h"
#include "img_queue.h"
#include "iot_button.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cam_helper.h"
#include "monitor_mamager.h"
#include "task_manager.h"

#define TAG "PAGE_CAM"
#define CAM_BUTTON_GPIO GPIO_NUM_0
#define JPEG_MAX_SIZE (80 * 1024)

typedef enum {
  CAM_STATE_PREVIEW = 0,
  CAM_STATE_CAPTURED,
} cam_state_t;

static cam_state_t s_state = CAM_STATE_PREVIEW;

static cam_shared_ctx_t *s_shared_ctx = NULL;

static lv_obj_t *img_obj = NULL;
static lv_obj_t *state_label = NULL;
static lv_image_dsc_t img_dsc;

static uint8_t *preview_buf = NULL;
static camera_fb_t *captured_fb = NULL;

static TaskHandle_t fetch_task_handle = NULL;
static button_handle_t btn = NULL;

/* =========================================================
                        Camera Resource Layer
   ========================================================= */

#include "vector.h"
#define CHUNK_SIZE 32

#define ALIGN_16 __attribute__((aligned(16)))

#define FAST_CLAMP(x) ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

static void downsample_2x_simd_optimized(camera_fb_t *fb) {
  if (!fb || !preview_buf)
    return;

  uint8_t *src = fb->buf;
  uint16_t *dst = (uint16_t *)preview_buf;
  int src_w = fb->width; // 320

  // 初始化 SIMD 向量栈
  VECTOR_STACK_INIT(vec_y, CHUNK_SIZE, DTYPE_INT32);
  VECTOR_STACK_INIT(vec_u, CHUNK_SIZE, DTYPE_INT32);
  VECTOR_STACK_INIT(vec_v, CHUNK_SIZE, DTYPE_INT32);
  VECTOR_STACK_INIT(vec_r, CHUNK_SIZE, DTYPE_INT32);
  VECTOR_STACK_INIT(vec_g, CHUNK_SIZE, DTYPE_INT32);
  VECTOR_STACK_INIT(vec_b, CHUNK_SIZE, DTYPE_INT32);
  VECTOR_STACK_INIT(vec_tmp, CHUNK_SIZE, DTYPE_INT32);

  int32_t *y_ptr = (int32_t *)vec_y.data;
  int32_t *u_ptr = (int32_t *)vec_u.data;
  int32_t *v_ptr = (int32_t *)vec_v.data;

  for (int y = 0; y < 120; y++) {
    uint8_t *src_row = &src[(y * 2) * src_w * 2];
    uint16_t *dst_row = &dst[y * 160];

    for (int x = 0; x < 160; x += CHUNK_SIZE) {
      for (int i = 0; i < CHUNK_SIZE; i++) {
        int src_idx = (x + i) * 2 * 2;
        y_ptr[i] = src_row[src_idx];
        u_ptr[i] = (int32_t)src_row[src_idx + 1] - 128;
        v_ptr[i] = (int32_t)src_row[src_idx + 3] - 128;
      }

      // Y = (Y - 16) * 1164
      vec_add_scalar(&vec_y, -16, &vec_y);
      vec_mul_scalar(&vec_y, 1164, &vec_y, 0);

      // R = Y + 1596 * V
      vec_mul_scalar(&vec_v, 1596, &vec_r, 0);
      vec_add(&vec_y, &vec_r, &vec_r);

      // B = Y + 2018 * U
      vec_mul_scalar(&vec_u, 2018, &vec_b, 0);
      vec_add(&vec_y, &vec_b, &vec_b);

      // G = Y - 813 * V - 391 * U
      vec_mul_scalar(&vec_v, -813, &vec_g, 0);
      vec_mul_scalar(&vec_u, -391, &vec_tmp, 0);
      vec_add(&vec_g, &vec_tmp, &vec_g);
      vec_add(&vec_y, &vec_g, &vec_g);

      // 归一化 (>> 10)
      int32_t *r_res = (int32_t *)vec_r.data;
      int32_t *g_res = (int32_t *)vec_g.data;
      int32_t *b_res = (int32_t *)vec_b.data;

      for (int i = 0; i < CHUNK_SIZE; i++) {
        // 结果右移 10 位并限制在 0-255
        int r = FAST_CLAMP(r_res[i] >> 10);
        int g = FAST_CLAMP(g_res[i] >> 10);
        int b = FAST_CLAMP(b_res[i] >> 10);

        // swapped
        uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        dst_row[x + i] = (rgb565 << 8) | (rgb565 >> 8);
      }
    }
  }
}

static bool cam_driver_start(void) {
  if (cam_helper_acquire() != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed");
    return false;
  }
  ESP_LOGI(TAG, "Camera started");
  return true;
}

static void cam_driver_stop(void) {
  cam_helper_release();
  ESP_LOGI(TAG, "Camera stopped");
}

static void cam_stop_preview(void) {
  if (fetch_task_handle) {
    vTaskDelete(fetch_task_handle);
    fetch_task_handle = NULL;
    ESP_LOGI(TAG, "Preview task stopped");
  }
}

static void cam_start_preview(void);

static void cam_fetch_task(void *arg) {
  bool src_set = false;

  while (1) {

    if (!img_obj || s_state != CAM_STATE_PREVIEW) {
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    extern void downsample_2x_simd_optimized(camera_fb_t * fb);
    downsample_2x_simd_optimized(fb);

    if (lvgl_port_lock(0)) {
      if (!src_set) {
        lv_image_set_src(img_obj, &img_dsc);
        src_set = true;
      }
      lv_obj_invalidate(img_obj);
      lvgl_port_unlock();
    }

    esp_camera_fb_return(fb);
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

static void cam_start_preview(void) {
  if (fetch_task_handle == NULL) {
    xTaskCreate(cam_fetch_task, "cam_fetch", 4096, NULL, 4, &fetch_task_handle);
    ESP_LOGI(TAG, "Preview task started");
  }
}

/* =========================================================
                        UI Helpers
   ========================================================= */

static void update_state_label(void) {
  if (!state_label)
    return;

  const char *text = (s_state == CAM_STATE_PREVIEW) ? "拍照" : "已截图";

  if (lvgl_port_lock(0)) {
    lv_label_set_text(state_label, text);
    lvgl_port_unlock();
  }
}

/* =========================================================
                        Capture Logic
   ========================================================= */

static void cam_take_picture(void) {
  if (s_state != CAM_STATE_PREVIEW)
    return;

  /* 1. 停止预览任务 */
  cam_stop_preview();

  /* 2. 抓一帧 */
  captured_fb = esp_camera_fb_get();
  if (!captured_fb) {
    cam_start_preview();
    return;
  }

  /* 3. 停止 camera driver */
  cam_driver_stop();

  s_state = CAM_STATE_CAPTURED;
  update_state_label();

  extern void downsample_2x_simd_optimized(camera_fb_t * fb);
  downsample_2x_simd_optimized(captured_fb);

  if (lvgl_port_lock(0)) {
    lv_obj_invalidate(img_obj);
    lvgl_port_unlock();
  }

  ESP_LOGI(TAG, "Picture captured");
}

static void cam_resume_preview(void) {
  if (captured_fb) {
    esp_camera_fb_return(captured_fb);
    captured_fb = NULL;
  }

  cam_driver_start();
  cam_start_preview();

  s_state = CAM_STATE_PREVIEW;
  update_state_label();
}

/* =========================================================
                        Save & Upload
   ========================================================= */

static void cam_save_picture(void) {
  if (s_state != CAM_STATE_CAPTURED || !captured_fb)
    return;

  jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
  cfg.width = captured_fb->width;
  cfg.height = captured_fb->height;
  cfg.src_type = JPEG_PIXEL_FORMAT_YCbYCr;
  cfg.quality = 60;
  cfg.task_enable = false;

  jpeg_enc_handle_t enc;
  if (jpeg_enc_open(&cfg, &enc) != JPEG_ERR_OK)
    return;

  uint8_t *jpg_buf = jpeg_calloc_align(JPEG_MAX_SIZE, 16);
  if (!jpg_buf) {
    jpeg_enc_close(enc);
    return;
  }

  int out_len = 0;
  if (jpeg_enc_process(enc, captured_fb->buf, captured_fb->len, jpg_buf,
                       JPEG_MAX_SIZE, &out_len) == JPEG_ERR_OK) {

    char path[64];
    snprintf(path, sizeof(path), "/littlefs/%llu.jpg",
             esp_timer_get_time() / 1000ULL);

    FILE *f = fopen(path, "wb");
    if (f) {
      fwrite(jpg_buf, 1, out_len, f);
      fclose(f);
      ESP_LOGI(TAG, "Saved: %s", path);
    }
  }

  jpeg_free_align(jpg_buf);
  jpeg_enc_close(enc);

  /* 保存完恢复预览 */
  cam_resume_preview();
}

/* =========================================================
                        Button
   ========================================================= */

static void btn_single_cb(void *arg, void *usr_data) {
  if (s_state == CAM_STATE_PREVIEW)
    cam_take_picture();
  else
    cam_save_picture();
}

static void btn_long_cb(void *arg, void *usr_data) {
  if (s_state == CAM_STATE_CAPTURED)
    cam_resume_preview();
  else
    gs_nav_pop();
}

static void button_init(void) {
  const button_config_t btn_cfg = {0};
  const button_gpio_config_t gpio_cfg = {
      .gpio_num = CAM_BUTTON_GPIO,
      .active_level = 0,
  };

  if (iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn) == ESP_OK) {
    iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, btn_single_cb, NULL);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, btn_long_cb,
                           NULL);
  }
}

/* =========================================================
                        Page Lifecycle
   ========================================================= */

static void *page_cam_init(void *args) {
  monitor_task_pause();

  preview_buf = heap_caps_aligned_alloc(16, 160 * 120 * 2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  img_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  img_dsc.header.w = 160;
  img_dsc.header.h = 120;
  img_dsc.header.stride = 160 * 2;
  img_dsc.data_size = 160 * 120 * 2;
  img_dsc.data = preview_buf;

  button_init();

  cam_driver_start();
  cam_start_preview();

  return NULL;
}

static void page_cam_deinit(void *ctx) {
  cam_stop_preview();
  cam_driver_stop();

  if (preview_buf) {
    heap_caps_free(preview_buf);
    preview_buf = NULL;
  }

  if (btn) {
    iot_button_delete(btn);
    btn = NULL;
  }

  monitor_task_resume();
}

/* ========================================================= */

const gs_page_desc_t page_cam = {
    .init_cb = page_cam_init,
    .render_cb = NULL,
    .update_cb = NULL,
    .deinit_cb = page_cam_deinit,
};
