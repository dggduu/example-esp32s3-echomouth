#include "button_gpio.h" // GPIO 按钮配置
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "gs_nav.h"
#include "img_stack.h"
#include "iot_button.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

#include "cam_shared.h"
#define TAG "PAGE_CAM"
#define CAM_BUTTON_GPIO GPIO_NUM_0
#define JPEG_MAX_SIZE (80 * 1024)

#include <stdbool.h>

typedef enum {
  CAM_STATE_PREVIEW = 0,
  CAM_STATE_CAPTURED,
} cam_state_t;

static cam_state_t s_state = CAM_STATE_PREVIEW;

static cam_shared_ctx_t *s_shared_ctx = NULL;

static lv_obj_t *img_obj;
static lv_image_dsc_t img_dsc;

static uint8_t *preview_buf = NULL;
static camera_fb_t *captured_fb = NULL;

static TaskHandle_t fetch_task_handle = NULL;
static button_handle_t btn;

static lv_obj_t *state_label = NULL;

static void update_state_label(void) {
  if (!state_label)
    return;
  const char *text;
  if (s_state == CAM_STATE_PREVIEW) {
    text = "拍照";
  } else {
    text = "已截图";
  }
  if (lvgl_port_lock(0)) {
    lv_label_set_text(state_label, text);
    lvgl_port_unlock();
  }
}

static void downsample_2x(camera_fb_t *fb) {
  uint16_t *src = (uint16_t *)fb->buf;
  uint16_t *dst = (uint16_t *)preview_buf;

  for (int y = 0; y < 120; y++) {
    for (int x = 0; x < 160; x++) {
      uint16_t color = src[(y * 2) * 320 + (x * 2)];
      dst[y * 160 + x] = color;
    }
  }
}

static void cam_fetch_task(void *arg) {
  while (1) {
    if (s_state != CAM_STATE_PREVIEW) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    downsample_2x(fb);

    if (lvgl_port_lock(0)) {
      lv_image_set_src(img_obj, &img_dsc);
      lv_obj_invalidate(img_obj);
      lvgl_port_unlock();
    }

    esp_camera_fb_return(fb);
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

static void cam_take_picture(void) {
  if (s_state != CAM_STATE_PREVIEW)
    return;

  captured_fb = esp_camera_fb_get();
  if (!captured_fb)
    return;

  downsample_2x(captured_fb);

  if (lvgl_port_lock(0)) {
    lv_image_set_src(img_obj, &img_dsc);
    lv_obj_invalidate(img_obj);
    lvgl_port_unlock();
  }

  s_state = CAM_STATE_CAPTURED;
  update_state_label();
  ESP_LOGI(TAG, "Picture captured, waiting for save");
}

/*  保存 JPEG  */
// static void cam_save_picture(void) {
//   if (s_state != CAM_STATE_CAPTURED || !captured_fb)
//     return;

//   jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
//   cfg.width = captured_fb->width;
//   cfg.height = captured_fb->height;
//   cfg.src_type = JPEG_PIXEL_FORMAT_RGB565_BE;
//   cfg.quality = 80;
//   cfg.task_enable = false;

//   jpeg_enc_handle_t enc;
//   if (jpeg_enc_open(&cfg, &enc) != JPEG_ERR_OK)
//     return;

//   uint8_t *jpg_buf = jpeg_calloc_align(JPEG_MAX_SIZE, 16);
//   if (!jpg_buf) {
//     jpeg_enc_close(enc);
//     return;
//   }

//   int out_len = 0;
//   if (jpeg_enc_process(enc, captured_fb->buf, captured_fb->len, jpg_buf,
//                        JPEG_MAX_SIZE, &out_len) == JPEG_ERR_OK) {
//     if (out_len >= 4 && jpg_buf[0] == 0xFF && jpg_buf[1] == 0xD8 &&
//         jpg_buf[out_len - 2] == 0xFF && jpg_buf[out_len - 1] == 0xD9) {
//       char path[128];
//       sprintf(path, "/littlefs/%llu.jpg", esp_timer_get_time() / 1000ULL);

//       FILE *f = fopen(path, "wb");
//       if (f) {
//         fwrite(jpg_buf, 1, out_len, f);
//         fclose(f);
//         img_stack_push(path);
//         ESP_LOGI(TAG, "Saved OK: %s (%d bytes)", path, out_len);
//       }
//     } else {
//       ESP_LOGE(TAG, "JPEG magic check FAILED");
//     }
//   }

//   jpeg_free_align(jpg_buf);
//   jpeg_enc_close(enc);

//   esp_camera_fb_return(captured_fb);
//   captured_fb = NULL;

//   s_state = CAM_STATE_PREVIEW;
//   update_state_label();
// }

static void cam_save_picture(void) {
  if (s_state != CAM_STATE_CAPTURED || !captured_fb)
    return;

  jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
  cfg.width = captured_fb->width;
  cfg.height = captured_fb->height;
  cfg.src_type = JPEG_PIXEL_FORMAT_RGB565_BE;
  cfg.quality = 60;
  cfg.task_enable = false;

  jpeg_enc_handle_t enc;
  if (jpeg_enc_open(&cfg, &enc) != JPEG_ERR_OK)
    return;

  // 采用对齐的缓存要求
  uint8_t *jpg_buf = jpeg_calloc_align(JPEG_MAX_SIZE, 16);
  if (!jpg_buf) {
    jpeg_enc_close(enc);
    return;
  }

  int out_len = 0;
  if (jpeg_enc_process(enc, captured_fb->buf, captured_fb->len, jpg_buf,
                       JPEG_MAX_SIZE, &out_len) == JPEG_ERR_OK) {
    if (out_len >= 4 && jpg_buf[0] == 0xFF && jpg_buf[1] == 0xD8 &&
        jpg_buf[out_len - 2] == 0xFF && jpg_buf[out_len - 1] == 0xD9) {
      char path[64];
      snprintf(path, sizeof(path), "/littlefs/%llu.jpg",
               esp_timer_get_time() / 1000ULL);

      FILE *f = fopen(path, "wb");
      if (f) {
        fwrite(jpg_buf, 1, out_len, f);
        fclose(f);

        // 1. 组装上传任务
        img_job_t job = {0};
        strlcpy(job.path, path, sizeof(job.path));

        if (s_shared_ctx) {
          job.task_id = s_shared_ctx->task_id;
          job.ctx = s_shared_ctx;
          s_shared_ctx->is_finished = false; // 重置状态，等待后台任务更新
          s_shared_ctx->success = false;
        }

        // 2. 推入后台队列
        if (img_stack_push(&job)) {
          ESP_LOGI(TAG, "Saved and Queued: %s (%d bytes)", path, out_len);
          // 3. 需求点：完成操作流水线后，立即退出界面交由上级监测状态
          gs_nav_pop();
        } else {
          ESP_LOGE(TAG, "Failed to push image to upload stack");
        }
      }
    } else {
      ESP_LOGE(TAG, "JPEG magic check FAILED");
    }
  }

  jpeg_free_align(jpg_buf);
  jpeg_enc_close(enc);
  esp_camera_fb_return(captured_fb);
  captured_fb = NULL;
}

static void cam_cancel_capture(void) {
  if (s_state != CAM_STATE_CAPTURED)
    return;
  if (captured_fb) {
    esp_camera_fb_return(captured_fb);
    captured_fb = NULL;
  }
  s_state = CAM_STATE_PREVIEW;
  update_state_label();
  ESP_LOGI(TAG, "Capture canceled, back to preview");
}

static void btn_single_cb(void *arg, void *usr_data) {
  if (s_state == CAM_STATE_PREVIEW) {
    cam_take_picture();
  } else {
    cam_save_picture();
  }
}

static void btn_long_cb(void *arg, void *usr_data) {
  if (s_state == CAM_STATE_CAPTURED) {
    cam_cancel_capture();
    return;
  }
  gs_nav_pop();
}

static void button_init(void) {
  const button_config_t btn_cfg = {0};
  const button_gpio_config_t btn_gpio_cfg = {
      .gpio_num = CAM_BUTTON_GPIO,
      .active_level = 0,
      .enable_power_save = false,
  };

  esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &btn);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create button");
    return;
  }

  iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL,
                         (button_cb_t)btn_single_cb, NULL);
  iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL,
                         (button_cb_t)btn_long_cb, NULL);
}

static void on_btn_return_pop(lv_event_t *e) {
  gs_nav_pop(); // 退出相机页面
}

static void on_btn_take_photo(lv_event_t *e) { cam_take_picture(); }

static void on_btn_save_photo(lv_event_t *e) { cam_save_picture(); }

static void on_btn_back_to_preview(lv_event_t *e) { cam_cancel_capture(); }

static lv_obj_t *create_text_btn(lv_obj_t *parent, const char *text,
                                 lv_event_cb_t cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, LV_PCT(22), 40);
  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
  return btn;
}

// static void *page_cam_init(void *args) {
//   preview_buf = heap_caps_malloc(160 * 120 * 2, MALLOC_CAP_SPIRAM);
//   img_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
//   img_dsc.header.w = 160;
//   img_dsc.header.h = 120;
//   img_dsc.header.stride = 160 * 2;
//   img_dsc.data_size = 160 * 120 * 2;
//   img_dsc.data = preview_buf;

//   button_init();
//   xTaskCreate(cam_fetch_task, "cam_fetch", 4096, NULL, 4,
//   &fetch_task_handle);

//   return NULL;
// }

static void *page_cam_init(void *args) {

  if (s_shared_ctx == NULL) {
    s_shared_ctx =
        heap_caps_calloc(1, sizeof(cam_shared_ctx_t), MALLOC_CAP_INTERNAL);
    if (s_shared_ctx == NULL) {
      ESP_LOGE(TAG, "Failed to allocate shared context");
      return NULL;
    }
  }

  // 存taskId
  s_shared_ctx->task_id = *(int *)args;
  // s_shared_ctx->task_id = 4;

  preview_buf = heap_caps_malloc(160 * 120 * 2, MALLOC_CAP_SPIRAM);
  img_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  img_dsc.header.w = 160;
  img_dsc.header.h = 120;
  img_dsc.header.stride = 160 * 2;
  img_dsc.data_size = 160 * 120 * 2;
  img_dsc.data = preview_buf;

  button_init();
  xTaskCreate(cam_fetch_task, "cam_fetch", 4096, NULL, 4, &fetch_task_handle);

  return NULL;
}

static void page_cam_deinit(void *ctx) {
  if (fetch_task_handle)
    vTaskDelete(fetch_task_handle);
  if (captured_fb)
    esp_camera_fb_return(captured_fb);
  if (preview_buf)
    heap_caps_free(preview_buf);
  if (btn)
    iot_button_delete(btn);
}

/* ===================== 渲染 ===================== */
static lv_obj_t *page_cam_render(lv_obj_t *parent, void *ctx) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(cont, lv_color_white(), 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

  img_obj = lv_image_create(cont);
  lv_obj_set_width(img_obj, 160);
  lv_obj_set_height(img_obj, 120);

  state_label = lv_label_create(cont);
  lv_label_set_text(state_label, "拍照");
  lv_obj_set_style_text_color(state_label, lv_color_white(), 0);

  lv_obj_t *btn_cont = lv_obj_create(cont);
  lv_obj_set_size(btn_cont, LV_PCT(90), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn_cont, 0, 0);
  lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  create_text_btn(btn_cont, "返回", on_btn_return_pop);
  create_text_btn(btn_cont, "拍照", on_btn_take_photo);
  create_text_btn(btn_cont, "保存", on_btn_save_photo);
  create_text_btn(btn_cont, "预览", on_btn_back_to_preview);

  return cont;
}

const gs_page_desc_t page_cam = {
    .init_cb = page_cam_init,
    .render_cb = page_cam_render,
    .update_cb = NULL,
    .deinit_cb = page_cam_deinit,
};