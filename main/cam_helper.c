#include "cam_helper.h"
#include "esp32_s3_szp.h"
#include "esp_log.h"

static const char *TAG = "CAM_HELPER";

void cam_helper_init(void) { return bsp_camera_init(); }

camera_fb_t *cam_helper_get_fb(void) {
  return esp_camera_fb_get(); // 直接返回 PSRAM 中的帧句柄
}

void cam_helper_return_fb(camera_fb_t *fb) {
  if (fb) {
    esp_camera_fb_return(fb); // 必须归还，否则导致内存泄漏
  }
}

bool cam_helper_yuv422_to_rgb888(camera_fb_t *fb, uint8_t *rgb_buf) {
  return fmt2rgb888(fb->buf, fb->len, fb->format, rgb_buf);
}

bool cam_helper_yuv422_to_jpg(camera_fb_t *fb, uint8_t quality,
                              uint8_t **out_buf, size_t *out_len) {
  static cam_tools_t tools; // 静态变量保持编码器句柄
  static bool initialized = false;

  // 第一次调用时初始化编码器（假设分辨率不变）
  if (!initialized) {
    esp_err_t ret = cam_helper_enc_init(&tools, fb->width, fb->height);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "JPEG encoder init failed");
      return false;
    }
    initialized = true;
  }

  // 可选：更新编码质量（硬件编码器支持在 process 前修改）
  // jpeg_enc_set_quality(tools.enc_handle, quality);

  int out_size = 0;
  esp_err_t ret = cam_helper_yuv2jpg_hw(&tools, fb, &out_size);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "JPEG encode failed");
    return false;
  }

  // 将编码后的数据拷贝出来，因为下次编码会覆盖缓冲区
  *out_buf = (uint8_t *)heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM);
  if (!*out_buf) {
    ESP_LOGE(TAG, "Failed to allocate output buffer");
    return false;
  }
  memcpy(*out_buf, tools.out_jpg_buf, out_size);
  *out_len = out_size;
  return true;
}

esp_err_t cam_helper_enc_init(cam_tools_t *tools, uint16_t w, uint16_t h) {
  ESP_LOGI("MEM", "Internal Free: %d bytes, PSRAM Free: %d bytes",
           heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  jpeg_enc_config_t config = DEFAULT_JPEG_ENC_CONFIG();
  config.width = w;
  config.height = h;
  config.src_type = JPEG_PIXEL_FORMAT_YCbYCr;
  config.subsampling = JPEG_SUBSAMPLE_420;
  config.quality = 80;
  config.task_enable = false;
  config.hfm_task_core = 0;

  tools->out_jpg_buf = (uint8_t *)heap_caps_aligned_alloc(
      16, MAX_JPG_BUFFER_SIZE, MALLOC_CAP_SPIRAM);

  jpeg_error_t j_err = jpeg_enc_open(&config, &tools->enc_handle);
  if (j_err != JPEG_ERR_OK) {
    ESP_LOGE(TAG, "Failed to open JPEG encoder: %d", j_err);
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t cam_helper_yuv2jpg_hw(cam_tools_t *tools, camera_fb_t *fb,
                                int *out_size) {

  jpeg_error_t j_err =
      jpeg_enc_process(tools->enc_handle, fb->buf, fb->len, tools->out_jpg_buf,
                       MAX_JPG_BUFFER_SIZE, out_size);

  return (j_err == JPEG_ERR_OK) ? ESP_OK : ESP_FAIL;
}