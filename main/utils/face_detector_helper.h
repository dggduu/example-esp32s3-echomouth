#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FD_MAX_FACES 5
#define FD_MAX_KEYPOINTS 10

typedef struct {
  float box[4]; // x1, y1, x2, y2
  float score;
  int keypoint[FD_MAX_KEYPOINTS];
  int keypoint_count;
} face_info_t;

typedef struct {
  face_info_t faces[FD_MAX_FACES];
  int count;
} face_detect_results_t;

/**
 * @brief 初始化人脸检测 Helper
 */
bool face_detector_helper_init(int fb_width, int fb_height);

/**
 * @brief 启动持续检测与采集
 */
void face_detector_helper_start_continuous(void);

/**
 * @brief 停止持续检测与采集
 */
void face_detector_helper_stop_continuous(void);

/**
 * @brief 反初始化并释放内存
 */
void face_detector_helper_deinit(void);

/**
 * @brief 查询检测任务是否运行中
 */
bool face_detector_helper_is_running(void);

/**
 * @brief 获取最新的检测结果数据
 */
void face_detector_helper_get_results(face_detect_results_t *out);

/**
 * @brief 【新增】获取最新的 RGB565 帧图像（已在内部绘制好人脸框）
 *
 * @param dst_buf 目标缓冲区（调用方传入，大小须为 width * height * 2 字节）
 * @param width 期望拷贝的宽度（需与初始化时的 fb_width 一致）
 * @param height 期望拷贝的高度（需与初始化时的 fb_height 一致）
 * @return true 成功获取新帧，false 无新帧或获取失败
 */
bool face_detector_helper_get_latest_rgb565(uint16_t *dst_buf, int width,
                                            int height);

/**
 * @brief 在外部 RGB565 缓冲区上绘制缩放后的框（保留兼容）
 */
void face_detector_helper_draw_scaled_results_rgb565(uint16_t *buf,
                                                     int target_w, int target_h,
                                                     int src_w, int src_h);

#ifdef __cplusplus
}
#endif