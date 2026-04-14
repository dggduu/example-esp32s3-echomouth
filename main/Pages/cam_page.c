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
#include "gs_portal.h"
#include "img_queue.h"
#include "iot_button.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "monitor_mamager.h"
#include "s3_helper.h"
#include "task_manager.h"
#include "vector.h"

#define TAG "PAGE_CAM"
#define CAM_BUTTON_GPIO GPIO_NUM_0
#define JPEG_MAX_SIZE (80 * 1024)
#define CHUNK_SIZE 32
#define FAST_CLAMP(x) ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

typedef enum {
  CAM_STATE_PREVIEW = 0,
  CAM_STATE_CAPTURED,
} cam_state_t;

static cam_state_t s_state = CAM_STATE_PREVIEW;
static cam_shared_ctx_t *s_shared_ctx = NULL;

static lv_obj_t *img_obj = NULL;
static lv_image_dsc_t img_dsc;

static uint8_t *preview_buf = NULL;
static camera_fb_t *captured_fb = NULL;

static TaskHandle_t fetch_task_handle = NULL;
// static button_handle_t btn;
static lv_obj_t *state_label = NULL;

/* 预览帧尺寸 */
#define PREVIEW_W 160
#define PREVIEW_H 120
#define SRC_W 320
#define SRC_H 240

/* 临时 YUV 缓冲区，由预览任务独占管理，外部只读 */
static uint8_t *temp_yuv_buf = NULL;

/* 任务停止标志 */
static volatile bool s_stop_fetch = false;

static void update_state_label(void) {
  if (!state_label)
    return;
  const char *text = (s_state == CAM_STATE_PREVIEW) ? "拍照" : "已截图";
  if (lvgl_port_lock(0)) {
    lv_label_set_text(state_label, text);
    lvgl_port_unlock();
  }
}

static void manual_upload_complete(bool success, const char *image_key,
                                   void *user_data) {
  if (!user_data)
    return;

  // 1. 先转回发送时传入的真实类型
  upload_user_ctx_t *u_ctx = (upload_user_ctx_t *)user_data;
  // 2. 从包装结构中提取 UI 上下文
  cam_shared_ctx_t *ctx = (cam_shared_ctx_t *)u_ctx->callback_ctx;

  if (ctx) {
    ctx->success = success;
    ctx->is_finished = true;
    if (success && image_key) {
      strlcpy(ctx->image_key, image_key, sizeof(ctx->image_key));
    }
    if (ctx->done_sem) {
      xSemaphoreGive(ctx->done_sem); // 现在地址正确了
    }
  }
}

/* 从 YUV 源缓冲区下采样到 RGB565 preview 缓冲区 */
static void downsample_from_buffer(uint8_t *src, int src_w, int src_h,
                                   uint8_t *dst) {
  if (!src || !dst)
    return;

  uint16_t *dst16 = (uint16_t *)dst;
  int dst_w = PREVIEW_W;
  int dst_h = PREVIEW_H;
  int src_stride = src_w * 2;

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

  for (int y = 0; y < dst_h; y++) {
    uint8_t *src_row = &src[(y * 2) * src_stride];
    uint16_t *dst_row = &dst16[y * dst_w];

    for (int x = 0; x < dst_w; x += CHUNK_SIZE) {
      for (int i = 0; i < CHUNK_SIZE; i++) {
        int src_idx = (x + i) * 2 * 2;
        y_ptr[i] = src_row[src_idx];
        u_ptr[i] = (int32_t)src_row[src_idx + 1] - 128;
        v_ptr[i] = (int32_t)src_row[src_idx + 3] - 128;
      }

      vec_add_scalar(&vec_y, -16, &vec_y);
      vec_mul_scalar(&vec_y, 1164, &vec_y, 0);

      vec_mul_scalar(&vec_v, 1596, &vec_r, 0);
      vec_add(&vec_y, &vec_r, &vec_r);

      vec_mul_scalar(&vec_u, 2018, &vec_b, 0);
      vec_add(&vec_y, &vec_b, &vec_b);

      vec_mul_scalar(&vec_v, -813, &vec_g, 0);
      vec_mul_scalar(&vec_u, -391, &vec_tmp, 0);
      vec_add(&vec_g, &vec_tmp, &vec_g);
      vec_add(&vec_y, &vec_g, &vec_g);

      int32_t *r_res = (int32_t *)vec_r.data;
      int32_t *g_res = (int32_t *)vec_g.data;
      int32_t *b_res = (int32_t *)vec_b.data;

      for (int i = 0; i < CHUNK_SIZE; i++) {
        int r = FAST_CLAMP(r_res[i] >> 10);
        int g = FAST_CLAMP(g_res[i] >> 10);
        int b = FAST_CLAMP(b_res[i] >> 10);
        uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        dst_row[x + i] = (rgb565 << 8) | (rgb565 >> 8);
      }
    }
  }
}

/* 兼容旧接口 */
static void downsample_2x_simd_optimized(camera_fb_t *fb) {
  if (!fb || !preview_buf)
    return;
  downsample_from_buffer(fb->buf, fb->width, fb->height, preview_buf);
}

/* 预览任务（独占 temp_yuv_buf 的管理） */
static void cam_fetch_task(void *arg) {
  bool src_set = false;
  ESP_LOGI(TAG, "Fetch task started");

  // 分配临时缓冲区，任务独自管理
  uint8_t *local_temp = heap_caps_malloc(SRC_W * SRC_H * 2, MALLOC_CAP_SPIRAM);
  if (!local_temp) {
    ESP_LOGE(TAG, "Failed to allocate temp YUV buffer");
    fetch_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }
  temp_yuv_buf =
      local_temp; // 全局可见，供外部（如deinit）参考，但释放由本任务负责

  // 快速获取首帧
  for (int retry = 0; retry < 5 && !s_stop_fetch; retry++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      memcpy(local_temp, fb->buf, fb->len);
      esp_camera_fb_return(fb);
      downsample_from_buffer(local_temp, SRC_W, SRC_H, preview_buf);
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  // 主循环
  while (!s_stop_fetch) {
    if (!img_obj || s_state != CAM_STATE_PREVIEW) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // 拷贝数据
    memcpy(local_temp, fb->buf, fb->len);
    // 立即归还
    esp_camera_fb_return(fb);
    // 下采样
    downsample_from_buffer(local_temp, SRC_W, SRC_H, preview_buf);

    // 刷新UI
    if (lvgl_port_lock(pdMS_TO_TICKS(20))) {
      if (!src_set) {
        lv_image_set_src(img_obj, &img_dsc);
        src_set = true;
      }
      lv_obj_invalidate(img_obj);
      lvgl_port_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(30));
  }

  // 任务退出前清理自己的资源
  if (local_temp) {
    heap_caps_free(local_temp);
    temp_yuv_buf = NULL;
  }
  fetch_task_handle = NULL;
  ESP_LOGI(TAG, "Fetch task exited");
  vTaskDelete(NULL);
}

static void cam_take_picture(void) {
  if (s_state != CAM_STATE_PREVIEW)
    return;

  captured_fb = esp_camera_fb_get();
  if (!captured_fb)
    return;

  downsample_2x_simd_optimized(captured_fb);

  if (lvgl_port_lock(0)) {
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
  esp_err_t ret = jpeg_enc_process(enc, captured_fb->buf, captured_fb->len,
                                   jpg_buf, JPEG_MAX_SIZE, &out_len);

  if (ret == JPEG_ERR_OK) {
    if (out_len >= 4 && jpg_buf[0] == 0xFF && jpg_buf[1] == 0xD8 &&
        jpg_buf[out_len - 2] == 0xFF && jpg_buf[out_len - 1] == 0xD9) {

      char path[64];
      snprintf(path, sizeof(path), "/littlefs/%llu.jpg",
               esp_timer_get_time() / 1000ULL);

      FILE *f = fopen(path, "wb");
      if (f) {
        fwrite(jpg_buf, 1, out_len, f);
        fclose(f);

        img_job_t job = {0};
        strlcpy(job.path, path, sizeof(job.path));
        job.task_id = s_shared_ctx->task_id;
        job.priority = IMG_PRIORITY_HIGH;
        job.type = IMG_TYPE_MANUAL;
        job.on_complete = manual_upload_complete;
        job.user_data = s_shared_ctx;
        job.retry_count = 0;

        static upload_user_ctx_t user_ctx;
        user_ctx.type = UPLOAD_TYPE_MANUAL;
        user_ctx.device_id = 1; // TODO: 从 NVS 读取
        user_ctx.task_id = s_shared_ctx->task_id;
        user_ctx.callback_ctx = s_shared_ctx;
        job.user_data = &user_ctx;

        if (img_queue_push(&job)) {
          ESP_LOGI(TAG, "Manual upload queued: %s", path);

          if (xSemaphoreTake(s_shared_ctx->done_sem, pdMS_TO_TICKS(30000)) ==
              pdTRUE) {
            gs_toast_show(s_shared_ctx->success ? "上传成功" : "上传失败",
                          s_shared_ctx->success ? GS_TOAST_SUCCESS
                                                : GS_TOAST_FAILED);
            if (task_manager_complete(s_shared_ctx->task_id)) {
              ESP_LOGI(TAG, "Task %d completed", s_shared_ctx->task_id);
            } else {
              ESP_LOGW(TAG, "Failed to complete task %d",
                       s_shared_ctx->task_id);
            }
          } else {
            gs_toast_show("上传超时", GS_TOAST_FAILED);
          }
          gs_nav_pop();
        } else {
          ESP_LOGE(TAG, "Upload queue full");
          gs_toast_show("队列已满，稍后重试", GS_TOAST_FAILED);
        }
      } else {
        ESP_LOGE(TAG, "Failed to open file for writing");
      }
    }
  } else {
    ESP_LOGE(TAG, "JPEG encode failed");
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

/* --------------------- 页面生命周期 --------------------- */
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

  monitor_task_pause();
  s_shared_ctx->task_id = *(int *)args;

  // 创建/重置信号量
  if (s_shared_ctx->done_sem == NULL) {
    s_shared_ctx->done_sem = xSemaphoreCreateBinary();
    if (s_shared_ctx->done_sem == NULL) {
      ESP_LOGE(TAG, "Failed to create semaphore");
      return NULL;
    }
  }
  xSemaphoreTake(s_shared_ctx->done_sem, 0); // 确保初始为空

  // 分配预览缓冲区
  if (preview_buf) {
    heap_caps_free(preview_buf);
    preview_buf = NULL;
  }
  preview_buf = heap_caps_aligned_alloc(16, PREVIEW_W * PREVIEW_H * 2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (preview_buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate preview buffer");
    return NULL;
  }
  memset(preview_buf, 0x00, PREVIEW_W * PREVIEW_H * 2);

  // 设置图像描述符
  img_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  img_dsc.header.w = PREVIEW_W;
  img_dsc.header.h = PREVIEW_H;
  img_dsc.header.stride = PREVIEW_W * 2;
  img_dsc.data_size = PREVIEW_W * PREVIEW_H * 2;
  img_dsc.data = preview_buf;

  // 如果有旧任务残留，强制清理
  if (fetch_task_handle != NULL) {
    s_stop_fetch = true;
    int timeout = 50;
    while (fetch_task_handle != NULL && timeout-- > 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (fetch_task_handle != NULL) {
      vTaskDelete(fetch_task_handle);
      fetch_task_handle = NULL;
    }
  }

  return s_shared_ctx;
}

static lv_obj_t *page_cam_render(lv_obj_t *parent, void *ctx) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(cont, lv_color_white(), 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

  img_obj = lv_image_create(cont);
  lv_obj_set_width(img_obj, PREVIEW_W);
  lv_obj_set_height(img_obj, PREVIEW_H);
  lv_image_set_src(img_obj, &img_dsc);

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

  // 渲染完成后启动预览任务
  s_stop_fetch = false;
  if (fetch_task_handle == NULL) {
    BaseType_t ret = xTaskCreate(cam_fetch_task, "cam_fetch", 3072, NULL, 4,
                                 &fetch_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create fetch task");
    }
  }

  return cont;
}

static void page_cam_deinit(void *ctx) {
  // 通知预览任务停止
  s_stop_fetch = true;

  // 等待任务退出（最多500ms）
  int timeout = 50;
  while (fetch_task_handle != NULL && timeout-- > 0) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (fetch_task_handle != NULL) {
    vTaskDelete(fetch_task_handle);
    fetch_task_handle = NULL;
  }

  if (temp_yuv_buf != NULL) {
    // 极端情况：任务未运行或异常退出，手动释放
    heap_caps_free(temp_yuv_buf);
    temp_yuv_buf = NULL;
  }

  // 清理其他资源
  if (captured_fb) {
    esp_camera_fb_return(captured_fb);
    captured_fb = NULL;
  }
  if (preview_buf) {
    heap_caps_free(preview_buf);
    preview_buf = NULL;
  }
  img_dsc.data = NULL; // 防止野指针

  if (s_shared_ctx && s_shared_ctx->done_sem) {
    vSemaphoreDelete(s_shared_ctx->done_sem);
    s_shared_ctx->done_sem = NULL;
  }
  img_obj = NULL;
  monitor_task_resume();
}

const gs_page_desc_t page_cam = {
    .init_cb = page_cam_init,
    .render_cb = page_cam_render,
    .update_cb = NULL,
    .deinit_cb = page_cam_deinit,
};