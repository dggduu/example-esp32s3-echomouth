#include "StyleSheet.h"
#include "cam_helper.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "gs_nav.h"
#include "gs_portal.h"
#include "lvgl.h"
#include "misc/lv_color.h"
#include <inttypes.h>
#include <string.h>

static const char *TAG = "PAGE_CAM_TEST";

/* 页面 Context 结构体 */
typedef struct {
  lv_obj_t *img_preview;
  lv_obj_t *lbl_status;
  lv_image_dsc_t img_dsc;
  uint8_t *rgb565_buf;
  size_t rgb565_buf_size;
  cam_subscriber_handle_t cam_sub;
  bool new_frame_arrived;
  uint32_t frame_w;
  uint32_t frame_h;
} cam_test_page_ctx_t;

/* ---------------------------------------------------------------
 * 订阅者接收回调 (运行于 cam_capture 任务上下文)
 * --------------------------------------------------------------*/
static void on_cam_frame_cb(const camera_fb_t *fb, void *user_arg) {
  cam_test_page_ctx_t *ctx = (cam_test_page_ctx_t *)user_arg;
  if (!ctx || !fb)
    return;

  if (fb->format == PIXFORMAT_RGB565) {
    if (ctx->rgb565_buf_size < fb->len) {
      uint8_t *new_buf = heap_caps_realloc(ctx->rgb565_buf, fb->len,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (new_buf) {
        ctx->rgb565_buf = new_buf;
        ctx->rgb565_buf_size = fb->len;
      } else {
        return;
      }
    }

    memcpy(ctx->rgb565_buf, fb->buf, fb->len);
    ctx->frame_w = fb->width;
    ctx->frame_h = fb->height;
    ctx->new_frame_arrived = true;
  }
}

/* ---------------------------------------------------------------
 * LVGL 轮询刷新 (运行于 LVGL 任务上下文)
 * --------------------------------------------------------------*/
static void page_cam_test_update(void *ctx_ptr) {
  cam_test_page_ctx_t *ctx = (cam_test_page_ctx_t *)ctx_ptr;
  if (!ctx || !ctx->img_preview)
    return;

  if (ctx->new_frame_arrived) {
    ctx->new_frame_arrived = false;

    // 更新图像描述符
    ctx->img_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
    ctx->img_dsc.header.w = ctx->frame_w;
    ctx->img_dsc.header.h = ctx->frame_h;
    ctx->img_dsc.data_size = ctx->frame_w * ctx->frame_h * 2;
    ctx->img_dsc.data = ctx->rgb565_buf;

    if (lvgl_port_lock(0)) {
      lv_image_set_src(ctx->img_preview, &ctx->img_dsc);
      lv_image_set_scale(ctx->img_preview, 128); // 0.5 倍缩放

      uint32_t subs = cam_helper_get_subscriber_count();
      lv_label_set_text_fmt(ctx->lbl_status,
                            "流运行中 | %" PRIu32 "x%" PRIu32
                            " | 订阅者数: %" PRIu32,
                            ctx->frame_w, ctx->frame_h, subs);
      lvgl_port_unlock();
    }
  }
}

static void btn_back_click_event(lv_event_t *e) { gs_nav_pop(); }

/* ---------------------------------------------------------------
 * 生命周期函数
 * --------------------------------------------------------------*/
static void *page_cam_test_init(void *args) {
  cam_test_page_ctx_t *ctx = calloc(1, sizeof(cam_test_page_ctx_t));
  if (!ctx) {
    ESP_LOGE(TAG, "Failed to allocate context");
    return NULL;
  }

  // 预分配 RGB565 缓存
  ctx->rgb565_buf_size = 320 * 240 * 2;
  ctx->rgb565_buf = heap_caps_malloc(ctx->rgb565_buf_size,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!ctx->rgb565_buf) {
    ESP_LOGE(TAG, "Failed to allocate initial RGB565 buffer");
    free(ctx);
    return NULL;
  }

  // 向 cam_helper 订阅推帧，自动触发硬件上电
  ctx->cam_sub = cam_helper_subscribe(on_cam_frame_cb, ctx);
  if (!ctx->cam_sub) {
    ESP_LOGE(TAG, "Failed to subscribe camera stream");
  } else {
    ESP_LOGI(TAG, "Camera subscribed by UI Cam Test Page");
  }

  return ctx;
}

static lv_obj_t *page_cam_test_render(lv_obj_t *parent, void *ctx_ptr) {
  cam_test_page_ctx_t *ctx = (cam_test_page_ctx_t *)ctx_ptr;
  if (!ctx)
    return NULL;

  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_set_style_bg_color(root, lv_color_hex(0x121212), 0);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  /* Header 顶部栏 */
  lv_obj_t *top_bar = lv_obj_create(root);
  lv_obj_set_size(top_bar, LV_PCT(100), 45);
  lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top_bar, 0, 0);
  lv_obj_set_style_pad_hor(top_bar, 12, 0);

  lv_obj_t *back_btn = lv_btn_create(top_bar);
  lv_obj_set_size(back_btn, 32, 32);
  lv_obj_set_style_radius(back_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(back_btn, S_COLOR_SURFACE_MID, 0);
  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_center(back_label);
  lv_obj_add_event_cb(back_btn, btn_back_click_event, LV_EVENT_CLICKED, NULL);

  lv_obj_t *title = lv_label_create(top_bar);
  lv_label_set_text(title, "Camera Live Test");
  lv_obj_set_style_text_color(title, S_TEXT_PRIMARY, 0);
  lv_obj_set_style_margin_left(title, 10, 0);

  /* 主预览区 */
  lv_obj_t *main_cont = lv_obj_create(root);
  lv_obj_set_width(main_cont, LV_PCT(100));
  lv_obj_set_flex_grow(main_cont, 1);
  lv_obj_set_flex_flow(main_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(main_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(main_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(main_cont, 0, 0);

  ctx->img_preview = lv_image_create(main_cont);
  lv_obj_set_size(ctx->img_preview, 160, 120);
  lv_obj_set_style_border_width(ctx->img_preview, 1, 0);
  lv_obj_set_style_border_color(ctx->img_preview, S_COLOR_SURFACE_MID, 0);
  lv_image_set_scale(ctx->img_preview, 128);

  ctx->lbl_status = lv_label_create(main_cont);
  lv_label_set_text(ctx->lbl_status, "等待帧接收中...");
  lv_obj_set_style_text_color(ctx->lbl_status, S_TEXT_SECONDARY, 0);
  lv_obj_set_style_margin_top(ctx->lbl_status, 6, 0);

  return root;
}

static void page_cam_test_deinit(void *ctx_ptr) {
  cam_test_page_ctx_t *ctx = (cam_test_page_ctx_t *)ctx_ptr;
  if (!ctx)
    return;

  // 取消订阅，无订阅者时摄像头硬件自动关闭
  if (ctx->cam_sub) {
    cam_helper_unsubscribe(ctx->cam_sub);
    ctx->cam_sub = NULL;
  }

  if (ctx->rgb565_buf) {
    heap_caps_free(ctx->rgb565_buf);
    ctx->rgb565_buf = NULL;
  }

  ESP_LOGI(TAG, "Camera unsubscribed by UI Cam Test Page");
  free(ctx);
}

const gs_page_desc_t page_cam_test = {
    .init_cb = page_cam_test_init,
    .render_cb = page_cam_test_render,
    .update_cb = page_cam_test_update,
    .deinit_cb = page_cam_test_deinit,
};