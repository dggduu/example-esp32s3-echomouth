#ifndef __CAM_HELPER_H__
#define __CAM_HELPER_H__

#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_camera.h"
#include "esp_jpeg_enc.h"

void cam_helper_init(void);

camera_fb_t *cam_helper_get_fb(void);

void cam_helper_return_fb(camera_fb_t *fb);

#ifdef __cplusplus
}
#endif

#endif