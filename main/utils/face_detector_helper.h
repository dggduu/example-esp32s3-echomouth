#ifndef FACE_DETECTOT_HELPER_H
#define FACE_DETECTOT_HELPER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 AI 环境
 * @return 初始化成功返回 true
 */
bool face_detector_helper_init(void);

/**
 * @brief 触发单次检测推理
 * @return 任务通知是否发送成功
 */
bool face_detector_helper_trigger_detection(uint32_t timeout_ms);
/**
 * @brief 获取推理是否正在忙碌 (可选)
 */
bool face_detector_helper_is_busy(void);

bool face_detector_helper_has_recent_face(int max_age_ms);
void face_detector_helper_update_timestamp(void);

#ifdef __cplusplus
}
#endif

#endif