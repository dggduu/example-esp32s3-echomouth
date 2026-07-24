#ifndef CAM_HELPER_H
#define CAM_HELPER_H

#include "esp_camera.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t xclk_freq_hz;
  pixformat_t pixel_format;
  framesize_t frame_size;
  uint8_t fb_count;
  uint32_t auto_standby_ms;
} cam_helper_config_t;

// 帧到达订阅回调定义
typedef void (*cam_frame_cb_t)(const camera_fb_t *fb, void *user_arg);

// 订阅者句柄
typedef void *cam_subscriber_handle_t;

esp_err_t cam_helper_init(const cam_helper_config_t *config);
esp_err_t cam_helper_deinit(void);

// 注册订阅者：注册后 camera 自动开机并开始持续推帧
cam_subscriber_handle_t cam_helper_subscribe(cam_frame_cb_t cb, void *user_arg);

// 取消订阅者：当无任何订阅者时，自动关闭 camera 硬件进入 Standby
esp_err_t cam_helper_unsubscribe(cam_subscriber_handle_t handle);

bool cam_helper_is_hardware_powered(void);
uint32_t cam_helper_get_subscriber_count(void);

#ifdef __cplusplus
}
#endif

#endif // CAM_HELPER_H