#ifndef BSP_AUDIO_H
#define BSP_AUDIO_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化音频编解码器（ES8311 播放 + ES7210 录音）
 * @param tx_handle I2S 发送句柄（播放）
 * @param rx_handle I2S 接收句柄（录音）
 * @return ESP_OK 成功
 * @note 调用前必须确保音频电源已使能（bsp_audio_power_on）
 */
esp_err_t bsp_audio_init(void *tx_handle, void *rx_handle);

/**
 * @brief 设置播放音量 (0~100)
 */
esp_err_t bsp_audio_set_volume(int volume);

/**
 * @brief 设置录音增益 (dB)
 */
esp_err_t bsp_audio_set_mic_gain(float gain);

/**
 * @brief 播放音频数据（阻塞）
 */
esp_err_t bsp_audio_play(const uint8_t *data, int len);

/**
 * @brief 录音（阻塞）
 */
esp_err_t bsp_audio_record(uint8_t *buf, int len);

/**
 * @brief 关闭音频设备
 */
esp_err_t bsp_audio_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_AUDIO_H