#ifndef BSP_CAMERA_H
#define BSP_CAMERA_H

#include "esp_camera.h"
#include "esp_err.h"


#ifdef __cplusplus
extern "C" {
#endif

/* 电源控制 */
esp_err_t bsp_camera_power_up(void);
esp_err_t bsp_camera_power_down(void);

/* 初始化 (power_up 之后调用) */
esp_err_t bsp_camera_init(uint32_t xclk_freq_hz, pixformat_t pixel_format,
                          framesize_t frame_size, uint8_t fb_count);
esp_err_t bsp_camera_deinit(void);

/* 帧操作 */
camera_fb_t *bsp_camera_get_frame(void);
void bsp_camera_return_frame(camera_fb_t *fb);
const camera_sensor_info_t *bsp_camera_get_sensor_info(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_CAMERA_H
