#include "button_gpio.h"
#include "cam_shared.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "gs_nav.h"
#include "gs_portal.h" // toast
#include "img_queue.h"
// #include "img_stack.h"
#include "iot_button.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "s3_helper.h"

#define TAG "PAGE_CAM"
#define CAM_BUTTON_GPIO GPIO_NUM_0
#define JPEG_MAX_SIZE (80 * 1024)

typedef enum {
  CAM_STATE_PREVIEW = 0,
  CAM_STATE_CAPTURED,
} cam_state_t;

static cam_state_t s_state = CAM_STATE_PREVIEW;
static cam_shared_ctx_t *s_shared_ctx = NULL;

static lv_obj_t *img_obj = NULL; // 显式初始化为 NULL，用于多线程安全检查
static lv_image_dsc_t img_dsc;

static uint8_t *preview_buf = NULL;
static camera_fb_t *captured_fb = NULL;

static TaskHandle_t fetch_task_handle = NULL;
static button_handle_t btn;
static lv_obj_t *state_label = NULL;

static void update_state_label(void) {
  if (!state_label)
    return;
  const char *text = (s_state == CAM_STATE_PREVIEW) ? "拍照" : "已截图";
  if (lvgl_port_lock(0)) {
    lv_label_set_text(state_label, text);
    lvgl_port_unlock();
  }
}

// 上传完成回调（在 uploader 任务上下文中调用）
static void upload_done_callback(bool success, const char *image_key,
                                 void *user_data) {
  cam_shared_ctx_t *ctx = (cam_shared_ctx_t *)user_data;
  if (ctx) {
    ctx->success = success;
    ctx->is_finished = true;
    if (success && image_key) {
      strlcpy(ctx->image_key, image_key, sizeof(ctx->image_key));
    }
    // 唤醒等待的 UI 任务
    if (ctx->done_sem) {
      xSemaphoreGive(ctx->done_sem);
    }
  }
}

static void manual_upload_complete(bool success, const char *image_key,
                                   void *user_data) {
  cam_shared_ctx_t *ctx = (cam_shared_ctx_t *)user_data;
  if (ctx) {
    ctx->success = success;
    ctx->is_finished = true;
    if (success && image_key) {
      strlcpy(ctx->image_key, image_key, sizeof(ctx->image_key));
    }
    // 唤醒等待的 UI 任务
    if (ctx->done_sem) {
      xSemaphoreGive(ctx->done_sem);
    }
  }
}

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

static void cam_fetch_task(void *arg) {
  bool src_set = false; // 优化点：仅设置一次图片源标志位

  while (1) {
    // 修复点：阻断竞态条件，等待 UI 渲染完成再跑逻辑
    if (!img_obj || s_state != CAM_STATE_PREVIEW) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    downsample_2x_simd_optimized(fb); // 使用优化后的函数

    if (lvgl_port_lock(0)) {
      if (!src_set) {
        lv_image_set_src(img_obj, &img_dsc); // 优化点：只在第一次绑定数据源
        src_set = true;
      }
      lv_obj_invalidate(
          img_obj); // 优化点：只通知重绘，不重建结构，大幅减小锁争用
      lvgl_port_unlock();
    }

    esp_camera_fb_return(fb);
    vTaskDelay(pdMS_TO_TICKS(
        30)); // 修复点：强制延时出让 CPU1 资源，防止 Watchdog 触发
  }
}

static void cam_take_picture(void) {
  if (s_state != CAM_STATE_PREVIEW)
    return;

  captured_fb = esp_camera_fb_get();
  if (!captured_fb)
    return;

  downsample_2x_simd_optimized(captured_fb);

  if (lvgl_port_lock(0)) {
    // 捕获时同样只需要 invalidate 刷新画面，因为 src 早已绑定过 preview_buf
    lv_obj_invalidate(img_obj);
    lvgl_port_unlock();
  }

  s_state = CAM_STATE_CAPTURED;
  update_state_label();
  ESP_LOGI(TAG, "Picture captured, waiting for save");
}

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

    if (out_len >= 4 && jpg_buf[0] == 0xFF && jpg_buf[1] == 0xD8 &&
        jpg_buf[out_len - 2] == 0xFF && jpg_buf[out_len - 1] == 0xD9) {

      char path[64];
      snprintf(path, sizeof(path), "/littlefs/%llu.jpg",
               esp_timer_get_time() / 1000ULL);

      FILE *f = fopen(path, "wb");
      if (f) {
        fwrite(jpg_buf, 1, out_len, f);
        fclose(f);

        // 构建上传任务
        img_job_t job = {0};
        strlcpy(job.path, path, sizeof(job.path));
        job.task_id = s_shared_ctx->task_id;
        job.priority = IMG_PRIORITY_HIGH;
        job.type = IMG_TYPE_MANUAL;
        job.on_complete = manual_upload_complete;
        job.user_data = s_shared_ctx;
        job.retry_count = 0;

        // 构建上传用户上下文（需要分配内存，确保生命周期）
        static upload_user_ctx_t user_ctx;
        user_ctx.type = UPLOAD_TYPE_MANUAL;
        user_ctx.device_id = 1; // 从 NVS 读取
        user_ctx.task_id = s_shared_ctx->task_id;
        user_ctx.callback_ctx = s_shared_ctx;
        job.user_data = &user_ctx;

        if (img_queue_push(&job)) {
          ESP_LOGI(TAG, "Manual upload queued: %s", path);

          // 显示上传中 Toast
          gs_toast_show("上传中...", GS_TOAST_INFO);

          // 等待上传完成（最多 30 秒）
          if (xSemaphoreTake(s_shared_ctx->done_sem, pdMS_TO_TICKS(30000)) ==
              pdTRUE) {
            if (s_shared_ctx->success) {
              gs_toast_show("上传成功", GS_TOAST_SUCCESS);
            } else {
              gs_toast_show("上传失败", GS_TOAST_FAILED);
            }
          } else {
            gs_toast_show("上传超时", GS_TOAST_FAILED);
          }

          // 退出界面
          gs_nav_pop();
        } else {
          ESP_LOGE(TAG, "Upload queue full");
          gs_toast_show("队列已满，稍后重试", GS_TOAST_FAILED);
        }
      }
    }

    jpeg_free_align(jpg_buf);
    jpeg_enc_close(enc);
    esp_camera_fb_return(captured_fb);
    captured_fb = NULL;

    // 无论结果如何，退出相机界面
    gs_nav_pop();
  }
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

static void on_btn_return_pop(lv_event_t *e) { gs_nav_pop(); }
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

static void *page_cam_init(void *args) {
  if (s_shared_ctx == NULL) {
    s_shared_ctx =
        heap_caps_calloc(1, sizeof(cam_shared_ctx_t), MALLOC_CAP_INTERNAL);
    if (s_shared_ctx == NULL) {
      ESP_LOGE(TAG, "Failed to allocate shared context");
      return NULL;
    }
  }

  if (args == NULL) {
    ESP_LOGE(TAG, "Invalid args: task_id not provided");
    return NULL;
  }
  s_shared_ctx->task_id = *(int *)args;

  // 创建信号量（初始为0）
  if (s_shared_ctx->done_sem == NULL) {
    s_shared_ctx->done_sem = xSemaphoreCreateBinary();
    if (s_shared_ctx->done_sem == NULL) {
      ESP_LOGE(TAG, "Failed to create semaphore");
      return NULL;
    }
  }

  // 分配预览缓冲区
  if (preview_buf) {
    heap_caps_free(preview_buf);
    preview_buf = NULL;
  }
  preview_buf = heap_caps_aligned_alloc(16, 160 * 120 * 2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (preview_buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate preview buffer");
    return NULL;
  }

  // 设置图像描述符
  img_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  img_dsc.header.w = 160;
  img_dsc.header.h = 120;
  img_dsc.header.stride = 160 * 2;
  img_dsc.data_size = 160 * 120 * 2;
  img_dsc.data = preview_buf;

  // 初始化按钮和任务
  button_init();
  if (fetch_task_handle == NULL) {
    BaseType_t ret = xTaskCreate(cam_fetch_task, "cam_fetch", 2048, NULL, 4,
                                 &fetch_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create fetch task");
      return NULL;
    }
  }

  return s_shared_ctx;
}

static void page_cam_deinit(void *ctx) {
  if (fetch_task_handle) {
    vTaskDelete(fetch_task_handle);
    fetch_task_handle = NULL;
  }
  if (captured_fb) {
    esp_camera_fb_return(captured_fb);
    captured_fb = NULL;
  }
  if (preview_buf) {
    heap_caps_free(preview_buf);
    preview_buf = NULL;
  }
  if (btn) {
    iot_button_delete(btn);
    btn = NULL;
  }
  if (s_shared_ctx && s_shared_ctx->done_sem) {
    vSemaphoreDelete(s_shared_ctx->done_sem);
    s_shared_ctx->done_sem = NULL;
  }
  img_obj = NULL;
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