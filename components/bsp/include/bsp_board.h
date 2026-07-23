#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include "driver/i2s_std.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 板级总初始化（I2C、PCA9539、I2S、电池）
 */
esp_err_t bsp_board_init(void);

/**
 * @brief 使能音频电源并初始化音频编解码器
 */
esp_err_t bsp_board_audio_power_up(void);

/**
 * @brief 使能屏幕电源
 */
esp_err_t bsp_board_screen_power_up(void);

/**
 * @brief 使能摄像头电源
 */
esp_err_t bsp_board_camera_power_up(void);

/**
 * @brief 关闭所有电源并外设解初始化
 */
esp_err_t bsp_board_power_off_all(void);

/**
 * @brief 关闭所有电源
 *
 * @return esp_err_t
 */
esp_err_t bsp_board_power_off_and_deinit_all(void);

/**
 * @brief 获取 I2S 发送句柄（播放）
 */
i2s_chan_handle_t bsp_board_get_i2s_tx(void);

/**
 * @brief 获取 I2S 接收句柄（录音）
 */
i2s_chan_handle_t bsp_board_get_i2s_rx(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_BOARD_H