#include "cam_helper.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

#include "bsp_camera.h"

static const char *TAG = "CAM_HELPER";

#define MAX_SUBSCRIBERS 4

typedef struct {
  bool in_use;
  cam_frame_cb_t cb;
  void *user_arg;
} subscriber_t;

static cam_helper_config_t s_cfg;
static bool s_helper_inited = false;
static bool s_hw_powered = false;
static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_capture_task_handle = NULL;

static subscriber_t s_subscribers[MAX_SUBSCRIBERS];
static uint32_t s_sub_count = 0;

/* 内部私有：上电 */
static esp_err_t internal_power_up_hardware(void) {
  if (s_hw_powered)
    return ESP_OK;

  ESP_LOGI(TAG, "Powering UP camera hardware...");
  esp_err_t ret = bsp_camera_power_up();
  if (ret != ESP_OK)
    return ret;

  ret = bsp_camera_init(s_cfg.xclk_freq_hz, s_cfg.pixel_format,
                        s_cfg.frame_size, s_cfg.fb_count);
  if (ret != ESP_OK) {
    bsp_camera_power_down();
    return ret;
  }

  s_hw_powered = true;
  ESP_LOGI(TAG, "Camera powered UP");
  return ESP_OK;
}

/* 内部私有：下电 */
static void internal_power_down_hardware(void) {
  if (!s_hw_powered)
    return;

  ESP_LOGI(TAG, "Powering DOWN camera hardware...");
  bsp_camera_deinit();
  bsp_camera_power_down();
  s_hw_powered = false;
  ESP_LOGI(TAG, "Camera powered OFF");
}

/* 后台独占帧抓取与广播任务 */
static void cam_capture_task(void *pvParameters) {
  ESP_LOGI(TAG, "Camera capture worker task started");

  while (1) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t subs = s_sub_count;
    xSemaphoreGive(s_mutex);

    // 如果当前没有任何订阅者，硬件下电并进入高休眠状态
    if (subs == 0) {
      xSemaphoreTake(s_mutex, portMAX_DELAY);
      if (s_hw_powered) {
        internal_power_down_hardware();
      }
      xSemaphoreGive(s_mutex);

      // 被唤醒前每 500ms 巡检一次
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // 有订阅者，确保硬件上电
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!s_hw_powered) {
      if (internal_power_up_hardware() != ESP_OK) {
        xSemaphoreGive(s_mutex);
        vTaskDelay(pdMS_TO_TICKS(200));
        continue;
      }
    }
    xSemaphoreGive(s_mutex);

    // 唯一的 FB 获取点
    camera_fb_t *fb = bsp_camera_get_frame();
    if (fb) {
      xSemaphoreTake(s_mutex, portMAX_DELAY);
      // 广播给所有订阅者
      for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (s_subscribers[i].in_use && s_subscribers[i].cb) {
          s_subscribers[i].cb(fb, s_subscribers[i].user_arg);
        }
      }
      xSemaphoreGive(s_mutex);

      bsp_camera_return_frame(fb);
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

esp_err_t cam_helper_init(const cam_helper_config_t *config) {
  if (s_helper_inited)
    return ESP_OK;
  if (!config)
    return ESP_ERR_INVALID_ARG;

  s_cfg = *config;
  s_mutex = xSemaphoreCreateMutex();
  memset(s_subscribers, 0, sizeof(s_subscribers));
  s_sub_count = 0;
  s_hw_powered = false;

  xTaskCreatePinnedToCore(cam_capture_task, "cam_cap", 6144, NULL, 4,
                          &s_capture_task_handle, 1);

  s_helper_inited = true;
  ESP_LOGI(TAG, "Cam Helper initialized with subscriber model");
  return ESP_OK;
}

esp_err_t cam_helper_deinit(void) {
  if (!s_helper_inited)
    return ESP_OK;

  if (s_capture_task_handle) {
    vTaskDelete(s_capture_task_handle);
    s_capture_task_handle = NULL;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  internal_power_down_hardware();
  s_sub_count = 0;
  s_helper_inited = false;
  xSemaphoreGive(s_mutex);

  vSemaphoreDelete(s_mutex);
  s_mutex = NULL;

  return ESP_OK;
}

cam_subscriber_handle_t cam_helper_subscribe(cam_frame_cb_t cb,
                                             void *user_arg) {
  if (!s_helper_inited || !cb)
    return NULL;

  cam_subscriber_handle_t handle = NULL;
  xSemaphoreTake(s_mutex, portMAX_DELAY);

  for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
    if (!s_subscribers[i].in_use) {
      s_subscribers[i].in_use = true;
      s_subscribers[i].cb = cb;
      s_subscribers[i].user_arg = user_arg;
      s_sub_count++;
      handle = (cam_subscriber_handle_t)&s_subscribers[i];
      ESP_LOGI(TAG, "New subscriber added, total: %u", s_sub_count);
      break;
    }
  }

  xSemaphoreGive(s_mutex);
  return handle;
}

esp_err_t cam_helper_unsubscribe(cam_subscriber_handle_t handle) {
  if (!s_helper_inited || !handle)
    return ESP_ERR_INVALID_ARG;

  esp_err_t ret = ESP_ERR_NOT_FOUND;
  xSemaphoreTake(s_mutex, portMAX_DELAY);

  subscriber_t *target = (subscriber_t *)handle;
  if (target >= &s_subscribers[0] && target < &s_subscribers[MAX_SUBSCRIBERS]) {
    if (target->in_use) {
      target->in_use = false;
      target->cb = NULL;
      target->user_arg = NULL;
      if (s_sub_count > 0)
        s_sub_count--;
      ESP_LOGI(TAG, "Subscriber removed, remaining: %u", s_sub_count);
      ret = ESP_OK;
    }
  }

  xSemaphoreGive(s_mutex);
  return ret;
}

bool cam_helper_is_hardware_powered(void) {
  bool powered = false;
  if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
    powered = s_hw_powered;
    xSemaphoreGive(s_mutex);
  }
  return powered;
}

uint32_t cam_helper_get_subscriber_count(void) {
  uint32_t count = 0;
  if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
    count = s_sub_count;
    xSemaphoreGive(s_mutex);
  }
  return count;
}