#include "audio_helper.h"
#include "bsp_audio.h"
#include "bsp_board.h"
#include "bsp_pca9539.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "AUDIO_HELPER";

static bool s_audio_running = false;
static int s_ref_count = 0;
static SemaphoreHandle_t s_audio_mutex = NULL;

static void audio_helper_lock(void) {
  if (s_audio_mutex == NULL) {
    s_audio_mutex = xSemaphoreCreateMutex();
  }
  xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
}

static void audio_helper_unlock(void) { xSemaphoreGive(s_audio_mutex); }

esp_err_t audio_helper_acquire(void) {
  audio_helper_lock();

  if (s_audio_running) {
    s_ref_count++;
    ESP_LOGI(TAG, "Audio already running, ref=%d", s_ref_count);
    audio_helper_unlock();
    return ESP_OK;
  }

  bsp_pa_power_on();
  esp_err_t ret = bsp_board_audio_power_up();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Audio power up failed: %s", esp_err_to_name(ret));
    bsp_pa_power_off();
    audio_helper_unlock();
    return ret;
  }
  ret = bsp_audio_init(bsp_board_get_i2s_tx(), bsp_board_get_i2s_rx());
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Audio codec init failed: %s", esp_err_to_name(ret));
    bsp_pa_power_off();
    audio_helper_unlock();
    return ret;
  }

  s_audio_running = true;
  s_ref_count = 1;
  ESP_LOGI(TAG, "Audio started, ref=%d", s_ref_count);

  audio_helper_unlock();
  return ESP_OK;
}

void audio_helper_release(void) {
  audio_helper_lock();

  if (!s_audio_running) {
    audio_helper_unlock();
    return;
  }

  s_ref_count--;
  if (s_ref_count <= 0) {
    bsp_audio_deinit();
    bsp_pa_power_off();
    s_audio_running = false;
    s_ref_count = 0;
    ESP_LOGI(TAG, "Audio stopped");
  } else {
    ESP_LOGI(TAG, "Audio release, ref=%d", s_ref_count);
  }

  audio_helper_unlock();
}

bool audio_helper_is_running(void) { return s_audio_running; }
