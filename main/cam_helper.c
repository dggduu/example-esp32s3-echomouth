#include "cam_helper.h"
#include "bsp_camera.h"
#include "bsp_config.h"
#include "bsp_pca9539.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "CAM_HELPER";

static bool s_cam_running = false;
static int s_ref_count = 0;

static SemaphoreHandle_t s_cam_mutex = NULL;

static void cam_helper_lock(void) {
  if (s_cam_mutex == NULL) {
    s_cam_mutex = xSemaphoreCreateMutex();
  }
  xSemaphoreTake(s_cam_mutex, portMAX_DELAY);
}

static void cam_helper_unlock(void) { xSemaphoreGive(s_cam_mutex); }

esp_err_t cam_helper_acquire(void) {
  cam_helper_lock();

  if (s_cam_running) {
    s_ref_count++;
    ESP_LOGI(TAG, "Camera already running, ref=%d", s_ref_count);
    cam_helper_unlock();
    return ESP_OK;
  }

  bsp_cam_power_on();
  esp_err_t ret = bsp_camera_init(20 * 1000 * 1000, PIXFORMAT_YUV422,
                                   FRAMESIZE_QVGA, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(ret));
    bsp_cam_power_off();
    cam_helper_unlock();
    return ret;
  }

  s_cam_running = true;
  s_ref_count = 1;

  ESP_LOGI(TAG, "Camera started, ref=%d", s_ref_count);

  cam_helper_unlock();
  return ESP_OK;
}

void cam_helper_release(void) {
  cam_helper_lock();

  if (!s_cam_running) {
    cam_helper_unlock();
    return;
  }

  s_ref_count--;

  if (s_ref_count <= 0) {
    bsp_camera_deinit();
    bsp_cam_power_off();
    s_cam_running = false;
    s_ref_count = 0;
    ESP_LOGI(TAG, "Camera stopped");
  } else {
    ESP_LOGI(TAG, "Camera release, ref=%d", s_ref_count);
  }

  cam_helper_unlock();
}

bool cam_helper_is_running(void) { return s_cam_running; }

camera_fb_t *cam_helper_get_fb(void) {
  if (!s_cam_running) {
    ESP_LOGE(TAG, "Camera not running");
    return NULL;
  }
  return esp_camera_fb_get();
}

void cam_helper_return_fb(camera_fb_t *fb) {
  if (fb) {
    esp_camera_fb_return(fb);
  }
}
