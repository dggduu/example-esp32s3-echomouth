#include "cam_helper.h"
#include "esp32_s3_szp.h"
#include "esp_log.h"

static const char *TAG = "CAM_HELPER";

void cam_helper_init(void) { return bsp_camera_init(); }

camera_fb_t *cam_helper_get_fb(void) { return esp_camera_fb_get(); }

void cam_helper_return_fb(camera_fb_t *fb) {
  if (fb) {
    esp_camera_fb_return(fb);
  }
}
