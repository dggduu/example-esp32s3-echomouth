#ifndef __CAM_HELPER_H__
#define __CAM_HELPER_H__

#include "esp_camera.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cam_helper_acquire(void);
void cam_helper_release(void);

bool cam_helper_is_running(void);

camera_fb_t *cam_helper_get_fb(void);
void cam_helper_return_fb(camera_fb_t *fb);

#ifdef __cplusplus
}
#endif

#endif