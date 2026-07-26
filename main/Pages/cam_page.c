#include "button_gpio.h"
#include "cam_helper.h"
#include "cam_shared.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "font/lv_symbol_def.h"
#include "gs_nav.h"
#include "gs_portal.h"
#include "img_converters.h" // 导入 fmt2rgb888 格式转换库
#include "img_queue.h"
#include "iot_button.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "misc/lv_color.h"
#include "monitor_mamager.h"
#include "power_manager.h"
#include "s3_helper.h"
#include "task_manager.h"
#include "ui_circle_utils.h" // 导入系统统一圆屏工具库

#define TAG "PAGE_CAM"
#define JPEG_MAX_SIZE (80 * 1024)

// 针对 240x240 圆形屏设计的预览分辨率 (320x240 下采样 2 倍)
#define PREVIEW_W 160
#define PREVIEW_H 120
#define SRC_W 320
#define SRC_H 240

typedef enum {
  CAM_STATE_PREVIEW = 0,
  CAM_STATE_CAPTURED,
} cam_state_t;

typedef struct {
  cam_state_t state;
  cam_shared_ctx_t *shared_ctx;
  cam_subscriber_handle_t cam_sub;

  lv_obj_t *img_card; // 图像外层卡片容器
  lv_obj_t *img_obj;
  lv_image_dsc_t img_dsc;
  uint8_t *preview_buf; // 160x120 RGB565 预览缓存

  uint8_t *raw_rgb565_buf; // 320x240 RGB565 原始全分辨率缓存
  size_t raw_buf_size;
  bool new_frame_arrived;

  lv_obj_t *state_label;
  lv_obj_t *btn_take;
  lv_obj_t *btn_save;
  lv_obj_t *btn_cancel;
} cam_page_ctx_t;

static cam_page_ctx_t *s_ctx = NULL;

/* ---------------------------------------------------------------
 * RGB565 专用的 2x 邻域下采样算法 (320x240 -> 160x120)
 * --------------------------------------------------------------*/
static void downsample_rgb565_2x(const uint8_t *src, uint8_t *dst, int src_w,
                                 int src_h) {
  if (!src || !dst)
    return;

  const uint16_t *src16 = (const uint16_t *)src;
  uint16_t *dst16 = (uint16_t *)dst;
  int dst_w = src_w / 2;
  int dst_h = src_h / 2;

  for (int y = 0; y < dst_h; y++) {
    const uint16_t *src_row = src16 + (y * 2) * src_w;
    uint16_t *dst_row = dst16 + y * dst_w;
    for (int x = 0; x < dst_w; x++) {
      dst_row[x] = src_row[x * 2];
    }
  }
}

/* ---------------------------------------------------------------
 * 动态更新按键与 UI 状态（统一主题风格）
 * --------------------------------------------------------------*/
static void update_state_ui(cam_page_ctx_t *ctx) {
  if (!ctx)
    return;

  if (lvgl_port_lock(0)) {
    if (ctx->state_label) {
      lv_label_set_text(ctx->state_label, ctx->state == CAM_STATE_PREVIEW
                                              ? "实时预览"
                                              : "照片已锁定");
    }

    // 切换预览图片框的高亮样式
    if (ctx->img_card) {
      if (ctx->state == CAM_STATE_CAPTURED) {
        lv_obj_set_style_border_color(ctx->img_card, S_COLOR_PRIMARY, 0);
        lv_obj_set_style_border_width(ctx->img_card, 2, 0);
      } else {
        lv_obj_set_style_border_color(ctx->img_card, S_COLOR_OUTLINE_VARIANT,
                                      0);
        lv_obj_set_style_border_width(ctx->img_card, 1, 0);
      }
    }

    // 控制操作按钮显隐
    if (ctx->state == CAM_STATE_PREVIEW) {
      if (ctx->btn_take)
        lv_obj_clear_flag(ctx->btn_take, LV_OBJ_FLAG_HIDDEN);
      if (ctx->btn_save)
        lv_obj_add_flag(ctx->btn_save, LV_OBJ_FLAG_HIDDEN);
      if (ctx->btn_cancel)
        lv_obj_add_flag(ctx->btn_cancel, LV_OBJ_FLAG_HIDDEN);
    } else {
      if (ctx->btn_take)
        lv_obj_add_flag(ctx->btn_take, LV_OBJ_FLAG_HIDDEN);
      if (ctx->btn_save)
        lv_obj_clear_flag(ctx->btn_save, LV_OBJ_FLAG_HIDDEN);
      if (ctx->btn_cancel)
        lv_obj_clear_flag(ctx->btn_cancel, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
  }
}

/* ---------------------------------------------------------------
 * 相机订阅者推帧回调 (运行于 cam_capture 任务上下文)
 * --------------------------------------------------------------*/
static void on_cam_frame_cb(const camera_fb_t *fb, void *user_arg) {
  cam_page_ctx_t *ctx = (cam_page_ctx_t *)user_arg;
  if (!ctx || !fb)
    return;

  if (ctx->state == CAM_STATE_PREVIEW && fb->format == PIXFORMAT_RGB565) {
    if (ctx->raw_buf_size < fb->len) {
      uint8_t *new_buf = heap_caps_realloc(ctx->raw_rgb565_buf, fb->len,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (new_buf) {
        ctx->raw_rgb565_buf = new_buf;
        ctx->raw_buf_size = fb->len;
      } else {
        return;
      }
    }

    memcpy(ctx->raw_rgb565_buf, fb->buf, fb->len);
    ctx->new_frame_arrived = true;
  }
}

static void manual_upload_complete(bool success, const char *image_key,
                                   void *user_data) {
  if (!user_data)
    return;

  upload_user_ctx_t *u_ctx = (upload_user_ctx_t *)user_data;
  cam_shared_ctx_t *ctx = (cam_shared_ctx_t *)u_ctx->callback_ctx;

  if (ctx) {
    ctx->success = success;
    ctx->is_finished = true;
    if (success && image_key) {
      strlcpy(ctx->image_key, image_key, sizeof(ctx->image_key));
    }
    if (ctx->done_sem) {
      xSemaphoreGive(ctx->done_sem);
    }
  }
}

/* ---------------------------------------------------------------
 * 拍照 / 保存 / 取消 交互处理
 * --------------------------------------------------------------*/
static void cam_take_picture(cam_page_ctx_t *ctx) {
  if (!ctx || ctx->state != CAM_STATE_PREVIEW)
    return;

  ctx->state = CAM_STATE_CAPTURED;
  update_state_ui(ctx);
  ESP_LOGI(TAG, "Picture captured and locked, waiting for save");
}

static void cam_cancel_capture(cam_page_ctx_t *ctx) {
  if (!ctx || ctx->state != CAM_STATE_CAPTURED)
    return;

  ctx->state = CAM_STATE_PREVIEW;
  update_state_ui(ctx);
  ESP_LOGI(TAG, "Capture canceled, returned to live preview");
}

static void cam_save_picture(cam_page_ctx_t *ctx) {
  if (!ctx || ctx->state != CAM_STATE_CAPTURED || !ctx->raw_rgb565_buf)
    return;

  // 1. 从 PSRAM 申请临时 RGB888 空间（320 * 240 * 3 = 230.4 KB）
  size_t rgb888_len = SRC_W * SRC_H * 3;
  uint8_t *rgb888_buf =
      (uint8_t *)heap_caps_malloc(rgb888_len, MALLOC_CAP_SPIRAM);
  if (!rgb888_buf) {
    ESP_LOGE(TAG, "Failed to allocate SPIRAM memory for RGB888 buffer");
    gs_toast_show("内存不足", GS_TOAST_FAILED);
    return;
  }

  // 2. 将 RGB565 转换成 RGB888
  if (!fmt2rgb888(ctx->raw_rgb565_buf, ctx->raw_buf_size, PIXFORMAT_RGB565,
                  rgb888_buf)) {
    ESP_LOGE(TAG, "fmt2rgb888 convert failed");
    heap_caps_free(rgb888_buf);
    gs_toast_show("图像格式转换失败", GS_TOAST_FAILED);
    return;
  }

  // 3. 配置 JPEG 编码器（输入源为 JPEG_PIXEL_FORMAT_RGB888）
  jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
  cfg.width = SRC_W;
  cfg.height = SRC_H;
  cfg.src_type = JPEG_PIXEL_FORMAT_RGB888; // ✅ 使用符合标准的输入格式
  cfg.quality = 60;
  cfg.task_enable = false;

  jpeg_enc_handle_t enc = NULL;
  if (jpeg_enc_open(&cfg, &enc) != JPEG_ERR_OK) {
    ESP_LOGE(TAG, "Failed to open JPEG encoder");
    heap_caps_free(rgb888_buf);
    gs_toast_show("编码器初始化失败", GS_TOAST_FAILED);
    return;
  }

  uint8_t *jpg_buf = (uint8_t *)jpeg_calloc_align(JPEG_MAX_SIZE, 16);
  if (!jpg_buf) {
    jpeg_enc_close(enc);
    heap_caps_free(rgb888_buf);
    gs_toast_show("内存分配失败", GS_TOAST_FAILED);
    return;
  }

  int out_len = 0;
  esp_err_t ret = jpeg_enc_process(enc, rgb888_buf, rgb888_len, jpg_buf,
                                   JPEG_MAX_SIZE, &out_len);

  // 4. 编码结束，立即释放临时 RGB888 内存
  heap_caps_free(rgb888_buf);

  if (ret == JPEG_ERR_OK && out_len >= 4 && jpg_buf[0] == 0xFF &&
      jpg_buf[1] == 0xD8 && jpg_buf[out_len - 2] == 0xFF &&
      jpg_buf[out_len - 1] == 0xD9) {

    char path[64];
    snprintf(path, sizeof(path), "/littlefs/%llu.jpg",
             (unsigned long long)(esp_timer_get_time() / 1000ULL));

    FILE *f = fopen(path, "wb");
    if (f) {
      fwrite(jpg_buf, 1, out_len, f);
      fclose(f);

      img_job_t job = {0};
      strlcpy(job.path, path, sizeof(job.path));
      job.task_id = ctx->shared_ctx->task_id;
      job.priority = IMG_PRIORITY_HIGH;
      job.type = IMG_TYPE_MANUAL;
      job.on_complete = manual_upload_complete;
      job.retry_count = 0;

      static upload_user_ctx_t user_ctx;
      user_ctx.type = UPLOAD_TYPE_MANUAL;
      user_ctx.device_id = ctx->shared_ctx->device_id;
      user_ctx.task_id = ctx->shared_ctx->task_id;
      user_ctx.callback_ctx = ctx->shared_ctx;
      job.user_data = &user_ctx;

      if (img_queue_push(&job)) {
        ESP_LOGI(TAG, "Manual upload queued: %s", path);

        if (xSemaphoreTake(ctx->shared_ctx->done_sem, pdMS_TO_TICKS(30000)) ==
            pdTRUE) {
          gs_toast_show(ctx->shared_ctx->success ? "上传成功" : "上传失败",
                        ctx->shared_ctx->success ? GS_TOAST_SUCCESS
                                                 : GS_TOAST_FAILED);
          if (task_manager_complete(ctx->shared_ctx->task_id)) {
            ESP_LOGI(TAG, "Task %d completed", ctx->shared_ctx->task_id);
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
      ESP_LOGE(TAG, "Failed to open file for writing: %s", path);
      gs_toast_show("保存文件失败", GS_TOAST_FAILED);
    }
  } else {
    ESP_LOGE(TAG, "JPEG encode failed, ret=%d", ret);
    gs_toast_show("JPEG 编码失败", GS_TOAST_FAILED);
  }

  jpeg_free_align(jpg_buf);
  jpeg_enc_close(enc);
}

/* 事件回调桥接 */
static void on_btn_return_pop(lv_event_t *e) { gs_nav_pop(); }
static void on_btn_take_photo(lv_event_t *e) { cam_take_picture(s_ctx); }
static void on_btn_save_photo(lv_event_t *e) { cam_save_picture(s_ctx); }
static void on_btn_back_to_preview(lv_event_t *e) { cam_cancel_capture(s_ctx); }

/* ---------------------------------------------------------------
 * 页面生命周期实现
 * --------------------------------------------------------------*/
static void *page_cam_init(void *args) {
  if (args == NULL) {
    ESP_LOGE(TAG, "Invalid args: cam_page_args_t required");
    return NULL;
  }

  cam_page_ctx_t *ctx = calloc(1, sizeof(cam_page_ctx_t));
  if (!ctx) {
    ESP_LOGE(TAG, "Failed to allocate page context");
    return NULL;
  }

  cam_page_args_t *page_args = (cam_page_args_t *)args;

  ctx->shared_ctx =
      heap_caps_calloc(1, sizeof(cam_shared_ctx_t), MALLOC_CAP_INTERNAL);
  if (!ctx->shared_ctx) {
    ESP_LOGE(TAG, "Failed to allocate shared context");
    free(ctx);
    return NULL;
  }

  monitor_task_pause();
  power_manager_report_activity();

  ctx->shared_ctx->task_id = page_args->task_id;
  ctx->shared_ctx->device_id = page_args->device_id;
  ctx->shared_ctx->done_sem = xSemaphoreCreateBinary();

  ctx->raw_buf_size = SRC_W * SRC_H * 2;
  ctx->raw_rgb565_buf =
      heap_caps_malloc(ctx->raw_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  ctx->preview_buf = heap_caps_aligned_alloc(
      16, PREVIEW_W * PREVIEW_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (!ctx->raw_rgb565_buf || !ctx->preview_buf || !ctx->shared_ctx->done_sem) {
    ESP_LOGE(TAG, "Failed to allocate buffers or semaphore");
    if (ctx->shared_ctx->done_sem)
      vSemaphoreDelete(ctx->shared_ctx->done_sem);
    if (ctx->raw_rgb565_buf)
      heap_caps_free(ctx->raw_rgb565_buf);
    if (ctx->preview_buf)
      heap_caps_free(ctx->preview_buf);
    free(ctx->shared_ctx);
    free(ctx);
    return NULL;
  }

  ctx->img_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  ctx->img_dsc.header.w = PREVIEW_W;
  ctx->img_dsc.header.h = PREVIEW_H;
  ctx->img_dsc.header.stride = PREVIEW_W * 2;
  ctx->img_dsc.data_size = PREVIEW_W * PREVIEW_H * 2;
  ctx->img_dsc.data = ctx->preview_buf;

  ctx->state = CAM_STATE_PREVIEW;
  s_ctx = ctx;

  ctx->cam_sub = cam_helper_subscribe(on_cam_frame_cb, ctx);
  if (!ctx->cam_sub) {
    ESP_LOGE(TAG, "Failed to subscribe camera stream");
  }

  return ctx;
}

static lv_obj_t *page_cam_render(lv_obj_t *parent, void *ctx_in) {
  cam_page_ctx_t *ctx = (cam_page_ctx_t *)ctx_in;
  if (!ctx)
    return NULL;

  // 1. 根页面容器：采用统一的 Surface 背景色
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(cont, S_COLOR_SURFACE_CONTAINER, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  // 2. 挂载左上角统一的退出/返回按钮到根节点
  ui_circle_add_exit_btn(cont, on_btn_return_pop);

  // 3. 顶部状态胶囊（系统的 Tag/Chip 样式）
  ctx->state_label = lv_label_create(cont);
  lv_obj_align(ctx->state_label, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_text_color(ctx->state_label, S_COLOR_ON_SURFACE, 0);
  lv_obj_set_style_bg_color(ctx->state_label, S_COLOR_SURFACE_CONTAINER_HIGH,
                            0);
  lv_obj_set_style_bg_opa(ctx->state_label, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(ctx->state_label, 12, 0);
  lv_obj_set_style_pad_ver(ctx->state_label, 4, 0);
  lv_obj_set_style_radius(ctx->state_label, LV_RADIUS_CIRCLE, 0);

  // 4. 卡片式图像预览外框（提升圆形屏视觉聚集效果）
  ctx->img_card = lv_obj_create(cont);
  lv_obj_set_size(ctx->img_card, PREVIEW_W + 4, PREVIEW_H + 4);
  lv_obj_center(ctx->img_card);
  lv_obj_set_style_bg_color(ctx->img_card, lv_color_black(), 0);
  lv_obj_set_style_radius(ctx->img_card, 12, 0);
  lv_obj_set_style_pad_all(ctx->img_card, 0, 0);
  lv_obj_clear_flag(ctx->img_card, LV_OBJ_FLAG_SCROLLABLE);

  ctx->img_obj = lv_image_create(ctx->img_card);
  lv_obj_set_size(ctx->img_obj, PREVIEW_W, PREVIEW_H);
  lv_obj_center(ctx->img_obj);
  lv_image_set_src(ctx->img_obj, &ctx->img_dsc);

  // 5. 底部操作栏（居中并对齐圆屏底部安全边距）
  lv_obj_t *btn_cont = lv_obj_create(cont);
  lv_obj_set_size(btn_cont, LV_PCT(100), 54);
  lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn_cont, 0, 0);
  lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(btn_cont, 16, 0); // 按钮间隔

  // 拍照按钮 (使用 Primary 主色)
  ctx->btn_take = ui_circle_create_fab(btn_cont, LV_SYMBOL_VIDEO,
                                       S_COLOR_PRIMARY, S_COLOR_ON_PRIMARY);
  lv_obj_add_event_cb(ctx->btn_take, on_btn_take_photo, LV_EVENT_CLICKED, NULL);

  // 重拍 / 取消按钮 (使用 Warning/Secondary 语义色)
  ctx->btn_cancel =
      ui_circle_create_fab(btn_cont, LV_SYMBOL_REFRESH,
                           S_COLOR_SURFACE_CONTAINER_HIGH, S_COLOR_ON_SURFACE);
  lv_obj_add_event_cb(ctx->btn_cancel, on_btn_back_to_preview, LV_EVENT_CLICKED,
                      NULL);

  // 保存 / 上传按钮 (使用 Success/Primary 色)
  ctx->btn_save = ui_circle_create_fab(btn_cont, LV_SYMBOL_OK, S_COLOR_PRIMARY,
                                       S_COLOR_ON_PRIMARY);
  lv_obj_add_event_cb(ctx->btn_save, on_btn_save_photo, LV_EVENT_CLICKED, NULL);

  update_state_ui(ctx);
  return cont;
}

static void page_cam_update(void *ctx_in) {
  cam_page_ctx_t *ctx = (cam_page_ctx_t *)ctx_in;
  if (!ctx || !ctx->img_obj)
    return;

  if (ctx->new_frame_arrived && ctx->state == CAM_STATE_PREVIEW) {
    ctx->new_frame_arrived = false;

    downsample_rgb565_2x(ctx->raw_rgb565_buf, ctx->preview_buf, SRC_W, SRC_H);

    if (lvgl_port_lock(0)) {
      lv_image_set_src(ctx->img_obj, &ctx->img_dsc);
      lv_obj_invalidate(ctx->img_obj);
      lvgl_port_unlock();
    }
  }
}

static void page_cam_deinit(void *ctx_in) {
  cam_page_ctx_t *ctx = (cam_page_ctx_t *)ctx_in;
  if (!ctx)
    return;

  if (ctx->cam_sub) {
    cam_helper_unsubscribe(ctx->cam_sub);
    ctx->cam_sub = NULL;
  }

  if (ctx->raw_rgb565_buf) {
    heap_caps_free(ctx->raw_rgb565_buf);
    ctx->raw_rgb565_buf = NULL;
  }

  if (ctx->preview_buf) {
    heap_caps_free(ctx->preview_buf);
    ctx->preview_buf = NULL;
  }

  if (ctx->shared_ctx) {
    if (ctx->shared_ctx->done_sem) {
      vSemaphoreDelete(ctx->shared_ctx->done_sem);
      ctx->shared_ctx->done_sem = NULL;
    }
    free(ctx->shared_ctx);
    ctx->shared_ctx = NULL;
  }

  s_ctx = NULL;
  monitor_task_resume();
  ESP_LOGI(TAG, "Camera page deinited and resources cleared");

  free(ctx);
}

const gs_page_desc_t page_cam = {
    .init_cb = page_cam_init,
    .render_cb = page_cam_render,
    .update_cb = page_cam_update,
    .deinit_cb = page_cam_deinit,
};