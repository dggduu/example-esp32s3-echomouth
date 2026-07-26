#include "http_client_helper.h"

#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mdns.h"
#include "sdkconfig.h"

#ifdef CONFIG_HTTP_MODE_HTTPS
#include "esp_crt_bundle.h"
#endif

#define TAG "HTTP_HELPER"

#define URL_MAX_LEN 256
#define HTTP_REV_BUF_SIZE 1024
#define MDNS_RESOLVE_TIMEOUT_MS 2000

#ifdef CONFIG_HTTP_MODE_MDNS
#define SERVER_HOSTNAME CONFIG_SERVER_HOSTNAME
#define SERVER_PORT CONFIG_SERVER_PORT
#endif

typedef struct {
  char *buffer;
  int max_len;
  int total_len;
} http_resp_ctx_t;

#ifdef CONFIG_HTTP_MODE_MDNS
static char s_cached_ip[32] = {0};
static SemaphoreHandle_t s_mutex = NULL;
#endif

/*---------------------------------------------------------------
 * mDNS 模块优化
 *--------------------------------------------------------------*/
#ifdef CONFIG_HTTP_MODE_MDNS

static bool resolve_server_ip(char *ip, size_t len) {
  esp_ip4_addr_t addr;
  char hostname[64];

  // 确保主机名格式正确
  snprintf(hostname, sizeof(hostname), "%.63s", SERVER_HOSTNAME);

  for (int i = 0; i < 3; i++) {
    if (mdns_query_a(hostname, MDNS_RESOLVE_TIMEOUT_MS, &addr) == ESP_OK) {
      snprintf(ip, len, IPSTR, IP2STR(&addr));
      ESP_LOGI(TAG, "mDNS Resolved %s -> %s", hostname, ip);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(300));
  }

  ESP_LOGE(TAG, "Failed to resolve mDNS hostname: %s", hostname);
  return false;
}

static bool get_server_ip(char *ip, size_t len) {
  if (!s_mutex) {
    ESP_LOGE(TAG, "mDNS mutex not initialized. Call http_helper_init() first.");
    return false;
  }

  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    ESP_LOGE(TAG, "Get mDNS mutex timeout");
    return false;
  }

  bool ret = true;
  if (s_cached_ip[0] == '\0') {
    if (!resolve_server_ip(s_cached_ip, sizeof(s_cached_ip))) {
      ret = false;
    }
  }

  if (ret) {
    strlcpy(ip, s_cached_ip, len);
  }

  xSemaphoreGive(s_mutex);
  return ret;
}

// 提供主动清除 IP 缓存接口（网络异常时调用）
static void invalidate_server_ip_cache(void) {
  if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    s_cached_ip[0] = '\0';
    xSemaphoreGive(s_mutex);
    ESP_LOGW(TAG, "mDNS IP cache invalidated.");
  }
}

#endif

/*---------------------------------------------------------------
 * HTTPS 配置优化
 *--------------------------------------------------------------*/
static void config_https(esp_http_client_config_t *config) {
#ifdef CONFIG_HTTP_MODE_HTTPS
  config->transport_type = HTTP_TRANSPORT_OVER_SSL;

#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE) ||                              \
    defined(CONFIG_USE_CERT_BUNDLE)
  config->crt_bundle_attach = esp_crt_bundle_attach;
#endif

#ifdef CONFIG_SERVER_CERT_PEM
  extern const uint8_t server_cert_pem_start[] asm(
      "_binary_server_cert_pem_start");
  config->cert_pem = (const char *)server_cert_pem_start;
#endif

#ifdef CONFIG_SKIP_COMMON_NAME_CHECK
  config->skip_cert_common_name_check = true;
#else
  config->skip_cert_common_name_check = false;
#endif

#endif
}

/*---------------------------------------------------------------
 * URL 构建
 *--------------------------------------------------------------*/
static bool build_url(char *url, size_t len, const char *path) {
  if (!path)
    path = "/";

#ifdef CONFIG_HTTP_MODE_MDNS
  char ip[32];
  if (!get_server_ip(ip, sizeof(ip))) {
    return false;
  }
  snprintf(url, len, "http://%s:%d%s", ip, SERVER_PORT, path);
#else
  snprintf(url, len, "%s%s", CONFIG_HTTPS_URL, path);
#endif

  return true;
}

/*---------------------------------------------------------------
 * HTTP 事件回调
 *--------------------------------------------------------------*/
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  http_resp_ctx_t *ctx = (http_resp_ctx_t *)evt->user_data;

  if (evt->event_id == HTTP_EVENT_ON_DATA) {
    if (!ctx || !ctx->buffer || ctx->max_len <= 0) {
      return ESP_OK;
    }

    // 防缓冲区溢出，保留 1 字节给 '\0'
    int remain = ctx->max_len - 1 - ctx->total_len;
    if (remain <= 0) {
      ESP_LOGW(TAG, "Response buffer full, truncation occurred.");
      return ESP_FAIL;
    }

    int copy_len = (evt->data_len > remain) ? remain : evt->data_len;
    memcpy(ctx->buffer + ctx->total_len, evt->data, copy_len);
    ctx->total_len += copy_len;
    ctx->buffer[ctx->total_len] = '\0'; // 动态补充 null 终止符
  }

  return ESP_OK;
}

/*---------------------------------------------------------------
 * Core Request 核心请求封装
 *--------------------------------------------------------------*/
static bool perform_request(esp_http_client_method_t method, const char *path,
                            const char *content_type, const void *body,
                            int body_len, char *response, int max_len) {
  char url[URL_MAX_LEN];

  if (!build_url(url, sizeof(url), path)) {
    ESP_LOGE(TAG, "Build URL failed");
    return false;
  }

  http_resp_ctx_t resp_ctx = {
      .buffer = response,
      .max_len = max_len,
      .total_len = 0,
  };

  if (response && max_len > 0) {
    response[0] = '\0';
  }

  esp_http_client_config_t config = {
      .url = url,
      .method = method,
      .timeout_ms = 15000,
      .event_handler = http_event_handler,
      .user_data = &resp_ctx,
      .buffer_size = HTTP_REV_BUF_SIZE,
  };

  config_https(&config);

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "Create client failed");
    return false;
  }

  if (content_type) {
    esp_http_client_set_header(client, "Content-Type", content_type);
  }

  if (body && body_len > 0) {
    esp_http_client_set_post_field(client, body, body_len);
  }

  esp_err_t err = esp_http_client_perform(client);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP request failed: %s | URL: %s", esp_err_to_name(err),
             url);
#ifdef CONFIG_HTTP_MODE_MDNS
    // 网络请求失败时清空 mDNS IP 缓存，防服务器换 IP 后无法恢复
    invalidate_server_ip_cache();
#endif
    esp_http_client_cleanup(client);
    return false;
  }

  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  // 放宽状态码判定至标准的 2xx 成功范围
  if (status >= 200 && status < 300) {
    return true;
  }

  ESP_LOGE(TAG, "HTTP status code error: %d | URL: %s", status, url);
  return false;
}

/*---------------------------------------------------------------
 * 公共 API 实现
 *--------------------------------------------------------------*/

bool http_helper_init(void) {
#ifdef CONFIG_HTTP_MODE_MDNS
  if (s_mutex == NULL) {
    s_mutex = xSemaphoreCreateMutex();
  }
  return (s_mutex != NULL);
#else
  return true;
#endif
}

bool http_get_json(const char *path, char *response, int max_len) {
  return perform_request(HTTP_METHOD_GET, path, NULL, NULL, 0, response,
                         max_len);
}

bool http_post_json(const char *path, const char *json) {
  int len = json ? strlen(json) : 0;
  return perform_request(HTTP_METHOD_POST, path, "application/json", json, len,
                         NULL, 0);
}

bool http_post_json_with_response(const char *path, const char *json,
                                  char *response, int max_len) {
  if (!path || !json || !response || max_len <= 0) {
    ESP_LOGE(TAG, "Invalid parameters");
    return false;
  }
  return perform_request(HTTP_METHOD_POST, path, "application/json", json,
                         strlen(json), response, max_len);
}

/*---------------------------------------------------------------
 * 二进制 PUT（带可选 Host 覆写）
 *--------------------------------------------------------------*/
bool http_put_binary_with_host(const char *url, uint8_t *data, int len,
                               const char *host_hdr) {
  if (!url || !data || len <= 0) {
    return false;
  }

  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_PUT,
      .timeout_ms = 15000,
      .buffer_size = 2048,
      .buffer_size_tx = 2048,
  };

  config_https(&config);

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "Create client failed");
    return false;
  }

  esp_http_client_set_header(client, "Content-Type", "image/jpeg");

  // 覆写 Host 头以匹配 S3 预签名 URL 的原始主机名
  if (host_hdr && host_hdr[0] != '\0') {
    esp_http_client_set_header(client, "Host", host_hdr);
    ESP_LOGI(TAG, "PUT Host override: %s", host_hdr);
  }

  esp_http_client_set_post_field(client, (const char *)data, len);

  esp_err_t err = esp_http_client_perform(client);
  bool success = false;

  if (err == ESP_OK) {
    int status = esp_http_client_get_status_code(client);
    if (status >= 200 && status < 300) {
      success = true;
    } else {
      ESP_LOGE(TAG, "PUT status failed: %d, URL: %s", status, url);
    }
  } else {
    ESP_LOGE(TAG, "PUT request failed: %s", esp_err_to_name(err));
  }

  esp_http_client_cleanup(client);
  return success;
}

bool http_put_binary(const char *url, uint8_t *data, int len) {
  return http_put_binary_with_host(url, data, len, NULL);
}

/*---------------------------------------------------------------
 * 辅助 API
 *--------------------------------------------------------------*/
bool get_mdns_server_ip(char *ip_buf, size_t buf_len) {
#ifdef CONFIG_HTTP_MODE_MDNS
  if (!ip_buf || buf_len == 0) {
    return false;
  }
  return get_server_ip(ip_buf, buf_len);
#else
  ESP_LOGW(TAG, "mDNS is not enabled in current build mode.");
  return false;
#endif
}

bool http_ping_server(void) {
  char url[URL_MAX_LEN];

  if (!build_url(url, sizeof(url), "/")) {
    ESP_LOGE(TAG, "Build URL failed");
    return false;
  }

  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_HEAD,
      .timeout_ms = 5000,
  };

  config_https(&config);

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "Create client failed");
    return false;
  }

  esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Ping failed: %s", esp_err_to_name(err));
#ifdef CONFIG_HTTP_MODE_MDNS
    invalidate_server_ip_cache();
#endif
    esp_http_client_cleanup(client);
    return false;
  }

  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (status >= 200 && status < 500) {
    ESP_LOGI(TAG, "Server reachable, status=%d", status);
    return true;
  }

  ESP_LOGE(TAG, "Server unreachable, status=%d", status);
  return false;
}