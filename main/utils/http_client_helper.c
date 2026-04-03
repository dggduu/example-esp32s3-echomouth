#include "http_client_helper.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mdns.h"
#include <string.h>

#define TAG "HTTP_HELPER"
#define SERVER_HOSTNAME "aobara-pc"
#define SERVER_PORT 3000
#define URL_MAX_LEN 256

typedef struct {
  char *buffer;
  int max_len;
  int total_len;
} http_resp_ctx_t;

static char s_cached_ip[32] = {0};
static SemaphoreHandle_t s_mutex = NULL;

/* ======================= mDNS ======================= */

static bool resolve_server_ip(char *ip, size_t len) {
  esp_ip4_addr_t addr;

  for (int i = 0; i < 3; i++) {
    if (mdns_query_a(SERVER_HOSTNAME, 2000, &addr) == ESP_OK) {
      snprintf(ip, len, IPSTR, IP2STR(&addr));
      ESP_LOGI(TAG, "Resolved %s.local -> %s", SERVER_HOSTNAME, ip);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(300));
  }

  ESP_LOGE(TAG, "mDNS resolve failed");
  return false;
}

static bool get_server_ip(char *ip, size_t len) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);

  if (s_cached_ip[0] == 0) {
    if (!resolve_server_ip(s_cached_ip, sizeof(s_cached_ip))) {
      xSemaphoreGive(s_mutex);
      return false;
    }
  }

  strncpy(ip, s_cached_ip, len);
  xSemaphoreGive(s_mutex);
  return true;
}

/* ======================= HTTP EVENT ======================= */

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  http_resp_ctx_t *ctx = (http_resp_ctx_t *)evt->user_data;

  switch (evt->event_id) {

  case HTTP_EVENT_ON_DATA:
    if (!ctx || !ctx->buffer)
      break;

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

/* ======================= CORE ======================= */

static bool perform_request(esp_http_client_method_t method, const char *path,
                            const char *content_type, const void *body,
                            int body_len, char *response, int max_len) {
  char ip[32];
  char url[URL_MAX_LEN];

  if (!get_server_ip(ip, sizeof(ip)))
    return false;

  snprintf(url, sizeof(url), "http://%s:%d%s", ip, SERVER_PORT, path);

  http_resp_ctx_t resp_ctx = {
      .buffer = response, .max_len = max_len, .total_len = 0};

  esp_http_client_config_t config = {
      .url = url,
      .method = method,
      .timeout_ms = 5000,
      .event_handler = http_event_handler,
      .user_data = &resp_ctx,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client)
    return false;

  if (content_type)
    esp_http_client_set_header(client, "Content-Type", content_type);

  if (body && body_len > 0)
    esp_http_client_set_post_field(client, body, body_len);

  esp_err_t err = esp_http_client_perform(client);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP perform failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }

  int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    ESP_LOGE(TAG, "HTTP status error: %d", status);
    ESP_LOGE(TAG, "HTTP URL: %s method:%d", url, (int)method);
    esp_http_client_cleanup(client);
    return false;
  }

  if (response && max_len > 0) {
    response[resp_ctx.total_len] = '\0';
  }

  esp_http_client_cleanup(client);
  return true;
}

/* ======================= API ======================= */

bool http_helper_init(void) {
  if (!s_mutex)
    s_mutex = xSemaphoreCreateMutex();

  return s_mutex != NULL;
}

bool http_get_json(const char *path, char *response, int max_len) {
  return perform_request(HTTP_METHOD_GET, path, NULL, NULL, 0, response,
                         max_len);
}

bool http_post_json(const char *path, const char *json) {
  return perform_request(HTTP_METHOD_POST, path, "application/json", json,
                         strlen(json), NULL, 0);
}
