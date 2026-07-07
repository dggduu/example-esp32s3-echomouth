#include "audio_helper.h"
#include "esp32_s3_szp.h"
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

  pa_en(1);
  esp_err_t ret = bsp_codec_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Audio codec init failed: %s", esp_err_to_name(ret));
    pa_en(0);
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
    bsp_codec_deinit();
    pa_en(0);
    s_audio_running = false;
    s_ref_count = 0;
    ESP_LOGI(TAG, "Audio stopped");
  } else {
    ESP_LOGI(TAG, "Audio release, ref=%d", s_ref_count);
  }

  audio_helper_unlock();
}

bool audio_helper_is_running(void) { return s_audio_running; }
