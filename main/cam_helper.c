#include "cam_helper.h"
#include "esp32_s3_szp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "CAM_HELPER";

/* ========= 内部状态 ========= */

static bool s_cam_running = false;
static int s_ref_count = 0;

static SemaphoreHandle_t s_cam_mutex = NULL;

/* ========= 内部工具 ========= */

static void cam_helper_lock(void) {
  if (s_cam_mutex == NULL) {
    s_cam_mutex = xSemaphoreCreateMutex();
  }
  xSemaphoreTake(s_cam_mutex, portMAX_DELAY);
}

static void cam_helper_unlock(void) { xSemaphoreGive(s_cam_mutex); }

/* ========= 对外接口 ========= */

esp_err_t cam_helper_acquire(void) {
  cam_helper_lock();

  if (s_cam_running) {
    s_ref_count++;
    ESP_LOGI(TAG, "Camera already running, ref=%d", s_ref_count);
    cam_helper_unlock();
    return ESP_OK;
  }

  bsp_camera_init();

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
    esp_camera_deinit();
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
