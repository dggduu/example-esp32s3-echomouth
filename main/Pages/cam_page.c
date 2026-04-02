#include "cam_helper.h"
#include "esp_heap_caps.h" // 解决 heap_caps_aligned_alloc 声明问题
#include "esp_jpeg_dec.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "gs_nav.h" // 必须包含你的导航系统头文件
#include "lvgl.h"   // 必须包含 LVGL 主头文件

static const char *TAG = "PAGE_CAM_HW";

typedef struct {
  lv_obj_t *img_canvas;
  lv_img_dsc_t img_dsc;

  // 硬件组件句柄
  cam_tools_t cam_tools;        // 之前封装的硬件编码器
  jpeg_dec_handle_t dec_handle; // 硬件解码器句柄

  // 缓冲区
  uint8_t *rgb_render_buf; // 解码后的显示内存 (16字节对齐)
  int outbuf_len;          // 解码器建议的输出长度
} cam_page_ctx_t;

// 1. 初始化页面：设置编解码器
static void *page_cam_init(void *args) {
  cam_page_ctx_t *ctx =
      heap_caps_calloc(1, sizeof(cam_page_ctx_t), MALLOC_CAP_SPIRAM);
  if (!ctx)
    return NULL;

  // A. 初始化硬件编码器 (YUV -> JPEG)
  cam_helper_enc_init(&ctx->cam_tools, 320, 240);

  // B. 初始化硬件解码器 (JPEG -> RGB565)
  jpeg_dec_config_t dec_config = DEFAULT_JPEG_DEC_CONFIG();
  dec_config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE; // 匹配 LVGL 16位色

  if (jpeg_dec_open(&dec_config, &ctx->dec_handle) != JPEG_ERR_OK) {
    ESP_LOGE(TAG, "JPEG Dec Open Failed");
    return NULL;
  }

  // C. 预分配对齐的显示缓冲区
  // RGB565 为 320*240*2 = 153600 字节
  ctx->rgb_render_buf =
      (uint8_t *)heap_caps_aligned_alloc(16, 320 * 240 * 2, MALLOC_CAP_SPIRAM);

  // D. 绑定 LVGL 描述符
  ctx->img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  ctx->img_dsc.header.w = 320;
  ctx->img_dsc.header.h = 240;
  ctx->img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  ctx->img_dsc.data_size = 320 * 240 * 2;
  ctx->img_dsc.data = ctx->rgb_render_buf;

  return ctx;
}

static lv_obj_t *page_cam_render(lv_obj_t *parent, void *ctx_ptr) {
  cam_page_ctx_t *ctx = (cam_page_ctx_t *)ctx_ptr;
  ctx->img_canvas = lv_img_create(parent);
  lv_obj_center(ctx->img_canvas);
  lv_img_set_src(ctx->img_canvas, &ctx->img_dsc);
  return ctx->img_canvas;
}

// 2. 核心逻辑：硬件闭环处理
static void page_cam_update(void *ctx_ptr) {
  cam_page_ctx_t *ctx = (cam_page_ctx_t *)ctx_ptr;
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb || !ctx->cam_tools.enc_handle || !ctx->dec_handle || !ctx)
    return;

  int jpg_size = 0;
  // Step 1: 硬件编码 (YUV422 -> JPEG)
  if (cam_helper_yuv2jpg_hw(&ctx->cam_tools, fb, &jpg_size) == ESP_OK) {

    // Step 2: 硬件解码 (JPEG -> RGB565)
    jpeg_dec_io_t decode_io = {
        .inbuf = ctx->cam_tools.out_jpg_buf, // 编码器的输出作为解码器的输入
        .inbuf_len = jpg_size,
        .outbuf = ctx->rgb_render_buf // 输出到显示缓冲区
    };

    // 解析头信息（可选，如果确定是QVGA可以简化，但规范要求调用）
    jpeg_dec_header_info_t header_info;
    if (jpeg_dec_parse_header(ctx->dec_handle, &decode_io, &header_info) ==
        JPEG_ERR_OK) {
      // 执行硬件解码
      if (jpeg_dec_process(ctx->dec_handle, &decode_io) == JPEG_ERR_OK) {
        // Step 3: 刷新 LVGL 界面
        lv_obj_invalidate(ctx->img_canvas);
      }
    }
  }

  esp_camera_fb_return(fb);
}

static void page_cam_deinit(void *ctx_ptr) {
  cam_page_ctx_t *ctx = (cam_page_ctx_t *)ctx_ptr;
  if (!ctx)
    return;

  // 释放硬件资源
  jpeg_enc_close(ctx->cam_tools.enc_handle);
  jpeg_dec_close(ctx->dec_handle);

  // 释放内存
  if (ctx->cam_tools.out_jpg_buf)
    free(ctx->cam_tools.out_jpg_buf);
  if (ctx->rgb_render_buf)
    free(ctx->rgb_render_buf);
  free(ctx);

  ESP_LOGI(TAG, "Page Cam HW Deinit Done");
}

const gs_page_desc_t page_cam = {.init_cb = page_cam_init,
                                 .render_cb = page_cam_render,
                                 .update_cb = page_cam_update,
                                 .deinit_cb = page_cam_deinit};