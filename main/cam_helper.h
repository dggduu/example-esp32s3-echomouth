#ifndef __CAM_HELPER_H__
#define __CAM_HELPER_H__

#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_camera.h"
#include "esp_jpeg_enc.h"

// 预分配的 JPEG 缓冲区大小 (QVGA 100KB 足够)
#define MAX_JPG_BUFFER_SIZE (100 * 1024)

typedef struct {
  jpeg_enc_handle_t enc_handle;
  uint8_t *out_jpg_buf; // 建议固定分配在 PSRAM
} cam_tools_t;

// 初始化编码器
esp_err_t cam_helper_enc_init(cam_tools_t *tools, uint16_t w, uint16_t h);
// 执行转换
esp_err_t cam_helper_yuv2jpg_hw(cam_tools_t *tools, camera_fb_t *fb,
                                int *out_size);

/**
 * @brief 摄像头辅助工具初始化
 * @return esp_err_t ESP_OK 为成功
 */
void cam_helper_init(void);

/**
 * @brief 获取当前摄像头原始帧 (YUV422)
 * @return camera_fb_t* 帧指针，使用完后必须调用 cam_helper_return_fb
 */
camera_fb_t *cam_helper_get_fb(void);

/**
 * @brief 释放帧缓冲区
 * @param fb 帧指针
 */
void cam_helper_return_fb(camera_fb_t *fb);

/**
 * @brief YUV422 转换为 JPEG (用于 S3 推送)
 * @param fb 原始 YUV422 帧
 * @param quality JPEG 质量 (1-100)
 * @param out_buf 输出 JPEG 数据的缓冲区指针 (由函数内部分配在堆上)
 * @param out_len 输出 JPEG 数据的长度
 * @return bool 是否转换成功
 */
bool cam_helper_yuv422_to_jpg(camera_fb_t *fb, uint8_t quality,
                              uint8_t **out_buf, size_t *out_len);

/**
 * @brief YUV422 转换为 RGB888 (用于 AI 模型输入)
 * @param fb 原始 YUV422 帧
 * @param rgb_buf 预先分配的缓冲区 (大小必须为 width * height * 3)
 * @return bool 是否转换成功
 */
bool cam_helper_yuv422_to_rgb888(camera_fb_t *fb, uint8_t *rgb_buf);

#ifdef __cplusplus
}
#endif

#endif