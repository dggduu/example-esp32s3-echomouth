#include "mdns_helper.h"
#include "gs_portal.h" // 引入 UI Toast 头文件

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include <string.h>

#define TAG "MDNS_HELPER"

static bool s_mdns_initialized = false;

/*---------------------------------------------------------------
 * mDNS 同步初始化 (阻塞直至完成)
 *--------------------------------------------------------------*/
bool mdns_helper_init(const char *local_hostname) {
  if (s_mdns_initialized) {
    return true;
  }

  ESP_LOGI(TAG, "Initializing mDNS synchronously...");

  ESP_LOGI(TAG, "BEFORE mdns_init()");

  esp_err_t err = mdns_init();

  ESP_LOGI(TAG, "AFTER mdns_init(), err=%s", esp_err_to_name(err));

  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "mDNS was already running.");
    s_mdns_initialized = true;
    return true;
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "BEFORE mdns_hostname_set()");
  err = mdns_hostname_set(local_hostname && strlen(local_hostname) > 0
                              ? local_hostname
                              : "esp32-s3");
  ESP_LOGI(TAG, "AFTER mdns_hostname_set(), err=%s", esp_err_to_name(err));

  ESP_LOGI(TAG, "BEFORE mdns_instance_name_set()");
  err = mdns_instance_name_set("ESP32-S3 Proj.Swan");
  ESP_LOGI(TAG, "AFTER mdns_instance_name_set(), err=%s", esp_err_to_name(err));

  s_mdns_initialized = true;

  ESP_LOGI(TAG, "mDNS initialized successfully!");

  return true;
}

bool mdns_helper_is_ready(void) { return s_mdns_initialized; }

/*---------------------------------------------------------------
 * 解析目标服务器 IP 地址 (带 5 次重试与 Toast 状态反馈)
 *--------------------------------------------------------------*/
bool mdns_helper_resolve_ip(const char *target_hostname, char *ip_buf,
                            size_t buf_len) {
  if (!target_hostname || !ip_buf || buf_len == 0)
    return false;

  if (!s_mdns_initialized) {
    ESP_LOGE(TAG, "mDNS is not initialized!");
    gs_toast_show("mDNS未就绪，无法寻找服务器", GS_TOAST_FAILED);
    return false;
  }

  esp_ip4_addr_t addr;
  char hostname_clean[64];
  snprintf(hostname_clean, sizeof(hostname_clean), "%.63s", target_hostname);

  // 重试机制（5 次，每次超时 1500ms，间隔 1000ms）
  const int max_retries = 5;
  for (int i = 0; i < max_retries; i++) {
    ESP_LOGI(TAG, "Resolving %s.local (Attempt %d/%d)...", hostname_clean,
             i + 1, max_retries);

    if (mdns_query_a(hostname_clean, 1500, &addr) == ESP_OK) {
      snprintf(ip_buf, buf_len, IPSTR, IP2STR(&addr));
      ESP_LOGI(TAG, "mDNS Resolved %s -> %s", hostname_clean, ip_buf);

      char toast_msg[64];
      snprintf(toast_msg, sizeof(toast_msg), "已找到服务器: %s", ip_buf);
      gs_toast_show(toast_msg, GS_TOAST_SUCCESS);
      return true;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  ESP_LOGE(TAG, "Failed to resolve mDNS hostname: %s after %d retries",
           hostname_clean, max_retries);
  gs_toast_show("寻找局域网服务器失败", GS_TOAST_FAILED);
  return false;
}