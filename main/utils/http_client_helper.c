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
 * mDNS
 *--------------------------------------------------------------*/

#ifdef CONFIG_HTTP_MODE_MDNS

static bool resolve_server_ip(char *ip, size_t len) {
  esp_ip4_addr_t addr;

  char hostname[64];
  snprintf(hostname, sizeof(hostname), "%s.local", SERVER_HOSTNAME);

  for (int i = 0; i < 3; i++) {
    if (mdns_query_a(hostname, 2000, &addr) == ESP_OK) {
      snprintf(ip, len, IPSTR, IP2STR(&addr));

      ESP_LOGI(TAG, "Resolved %s -> %s", hostname, ip);

      return true;
    }

    vTaskDelay(pdMS_TO_TICKS(300));
  }

  ESP_LOGE(TAG, "Failed to resolve %s", hostname);

  return false;
}

static bool get_server_ip(char *ip, size_t len) {
  if (!s_mutex) {
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
      return false;
    }
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);

  if (s_cached_ip[0] == '\0') {
    if (!resolve_server_ip(s_cached_ip, sizeof(s_cached_ip))) {
      xSemaphoreGive(s_mutex);
      return false;
    }
  }

  strlcpy(ip, s_cached_ip, len);

  xSemaphoreGive(s_mutex);

  return true;
}

#endif

/*---------------------------------------------------------------
 * HTTPS
 *--------------------------------------------------------------*/

#define CONFIG_USE_CERT_BUNDLE 1

static void config_https(esp_http_client_config_t *config) {

#ifdef CONFIG_HTTP_MODE_HTTPS

  config->transport_type = HTTP_TRANSPORT_OVER_SSL;

#ifdef CONFIG_USE_CERT_BUNDLE

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
 * URL Builder
 *--------------------------------------------------------------*/

static bool build_url(char *url, size_t len, const char *path) {

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
 * HTTP Event
 *--------------------------------------------------------------*/

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  http_resp_ctx_t *ctx = (http_resp_ctx_t *)evt->user_data;

  switch (evt->event_id) {

  case HTTP_EVENT_ON_DATA:

    if (!ctx || !ctx->buffer) {
      break;
    }

    if (ctx->total_len + evt->data_len >= ctx->max_len - 1) {
      ESP_LOGW(TAG, "Response buffer overflow");

      return ESP_FAIL;
    }

    memcpy(ctx->buffer + ctx->total_len, evt->data, evt->data_len);

    ctx->total_len += evt->data_len;

    break;

  default:
    break;
  }

  return ESP_OK;
}

/*---------------------------------------------------------------
 * Core Request
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
    ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));

    esp_http_client_cleanup(client);

    return false;
  }

  int status = esp_http_client_get_status_code(client);

  if (status != 200) {
    ESP_LOGE(TAG, "HTTP status=%d", status);

    ESP_LOGE(TAG, "URL=%s", url);

    esp_http_client_cleanup(client);

    return false;
  }

  if (response && max_len > 0) {
    response[resp_ctx.total_len] = '\0';
  }

  esp_http_client_cleanup(client);

  return true;
}
/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

bool http_helper_init(void) {
#ifdef CONFIG_HTTP_MODE_MDNS

  if (!s_mutex) {
    s_mutex = xSemaphoreCreateMutex();
  }

  return s_mutex != NULL;

#else

  return true;

#endif
}

bool http_get_json(const char *path, char *response, int max_len) {
  return perform_request(HTTP_METHOD_GET, path, NULL, NULL, 0, response,
                         max_len);
}

bool http_post_json(const char *path, const char *json) {
  return perform_request(HTTP_METHOD_POST, path, "application/json", json,
                         strlen(json), NULL, 0);
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
 * Binary PUT
 *--------------------------------------------------------------*/

bool http_put_binary(const char *url, uint8_t *data, int len) {
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

  /* HTTPS 自动配置 */
  config_https(&config);

  esp_http_client_handle_t client = esp_http_client_init(&config);

  if (!client) {
    ESP_LOGE(TAG, "Create client failed");
    return false;
  }

  esp_http_client_set_header(client, "Content-Type", "image/jpeg");

  esp_http_client_set_post_field(client, (const char *)data, len);

  esp_err_t err = esp_http_client_perform(client);

  bool success = false;

  if (err == ESP_OK) {
    int status = esp_http_client_get_status_code(client);

    if (status >= 200 && status < 300) {
      success = true;
    } else {
      ESP_LOGE(TAG, "PUT failed, status=%d", status);

      ESP_LOGE(TAG, "URL=%s", url);
    }
  } else {
    ESP_LOGE(TAG, "PUT failed: %s", esp_err_to_name(err));
  }

  esp_http_client_cleanup(client);

  return success;
}

/*---------------------------------------------------------------
 * mDNS
 *--------------------------------------------------------------*/

bool get_mdns_server_ip(char *ip_buf, size_t buf_len) {

#ifdef CONFIG_HTTP_MODE_MDNS

  if (!ip_buf || buf_len == 0) {
    return false;
  }

  return get_server_ip(ip_buf, buf_len);

#else

  ESP_LOGW(TAG, "Current mode is HTTPS.");

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
      .method = HTTP_METHOD_HEAD, // 或 HTTP_METHOD_GET
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
    ESP_LOGE(TAG, "HTTPS ping failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }

  int status = esp_http_client_get_status_code(client);

  esp_http_client_cleanup(client);

  if (status >= 200 && status < 500) {
    ESP_LOGI(TAG, "HTTPS server reachable, status=%d", status);
    return true;
  }

  ESP_LOGE(TAG, "HTTPS server unreachable, status=%d", status);
  return false;
}