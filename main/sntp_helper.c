#include "sntp_helper.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include <sys/time.h>
#include <time.h>

static const char *TAG = "sntp_helper";
static time_t s_last_timestamp = 0;

static void sync_cb(struct timeval *tv) {
  s_last_timestamp = tv->tv_sec;
  ESP_LOGI(TAG, "SNTP sync completed, time: %s", ctime(&s_last_timestamp));
}

esp_err_t sntp_helper_init(void) {
  esp_netif_sntp_deinit(); // Ensure clean state
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
  config.sync_cb = sync_cb;
  config.smooth_sync = false;
  esp_err_t ret = esp_netif_sntp_init(&config);
  if (ret != ESP_OK)
    return ret;

  ESP_LOGI(TAG, "SNTP component initialized");
  return ESP_OK;
}

esp_err_t sntp_helper_time(const char *server, int timeout_ms) {
  if (server == NULL) {
    ESP_LOGE(TAG, "Invalid server");
    return ESP_ERR_INVALID_ARG;
  }
  esp_sntp_stop();
  esp_sntp_setservername(0, server);
  esp_sntp_init();

  esp_err_t ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SNTP sync failed: %s", esp_err_to_name(ret));
    return ret;
  }

  time_t now = time(NULL);
  if (now < 8 * 3600 * 2) {
    ESP_LOGW(TAG, "Synchronized time seems invalid: %ld", now);
  } else {
    ESP_LOGI(TAG, "Time synchronized to %s", ctime(&now));
  }
  return ESP_OK;
}

esp_err_t sntp_helper_set_timezone(const char *tz_string) {
  if (tz_string == NULL) {
    ESP_LOGE(TAG, "Invalid timezone string");
    return ESP_ERR_INVALID_ARG;
  }

  if (setenv("TZ", tz_string, 1) != 0) {
    ESP_LOGE(TAG, "setenv failed");
    return ESP_FAIL;
  }
  tzset();
  ESP_LOGI(TAG, "Timezone set to: %s", tz_string);
  return ESP_OK;
}

time_t sntp_helper_get_last_timestamp(void) { return s_last_timestamp; }

uint64_t sntp_helper_get_timestamp_ms(void) {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) != 0) {
    ESP_LOGE(TAG, "gettimeofday failed");
    return 0;
  }
  return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void sntp_helper_deinit(void) {
  esp_netif_sntp_deinit();
  s_last_timestamp = 0;
  ESP_LOGI(TAG, "SNTP component deinitialized");
}