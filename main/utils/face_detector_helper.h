// face_detector_helper.h
#ifndef FACE_DETECTOR_HELPER_H
#define FACE_DETECTOR_HELPER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化人脸检测模块
 * @param fb_width  相机帧宽度（用于预分配缓冲区）
 * @param fb_height 相机帧高度
 * @return true 成功, false 失败
 */
bool face_detector_helper_init(int fb_width, int fb_height);

/**
 * @brief 反初始化，释放资源
 */
void face_detector_helper_deinit(void);

/**
 * @brief 触发一次人脸检测（同步）
 * @param timeout_ms 超时时间（毫秒）
 * @return true 检测到人脸, false 未检测到或超时/失败
 */
bool face_detector_helper_trigger_detection(uint32_t timeout_ms);

/**
 * @brief 查询检测器是否正忙
 * @return true 忙, false 空闲
 */
bool face_detector_helper_is_busy(void);

/**
 * @brief 手动更新时间戳（用于外部标记最近有人脸）
 */
void face_detector_helper_update_timestamp(void);

/**
 * @brief 查询最近是否检测到人脸
 * @param max_age_ms 最大有效时间（毫秒）
 * @return true 在有效时间内检测到过人脸, false 否则
 */
bool face_detector_helper_has_recent_face(int max_age_ms);

#ifdef __cplusplus
}
#endif

#endif // FACE_DETECTOR_HELPER_H