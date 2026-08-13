#include "http_client_helper.h"

#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

// 引入 UI 弹窗与 mDNS 模块
#include "gs_portal.h"

#ifdef CONFIG_HTTP_MODE_MDNS
#include "mdns_helper.h"
#endif

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
 * mDNS IP 缓存与解析逻辑 (强化互斥锁与初始化判定)
 *--------------------------------------------------------------*/
#ifdef CONFIG_HTTP_MODE_MDNS

static bool resolve_server_ip(char *ip, size_t len) {
  // 调用 mdns_helper 模块解析目标服务器 IP
  return mdns_helper_resolve_ip(SERVER_HOSTNAME, ip, len);
}

static bool get_server_ip(char *ip, size_t len) {
  // 1. 互斥锁二次安全防御：若未初始化则尝试兜底创建
  if (!s_mutex) {
    ESP_LOGW(TAG, "mDNS mutex was NULL, lazy initializing...");
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
      ESP_LOGE(TAG, "Create mDNS mutex failed!");
      gs_toast_show("系统锁创建失败", GS_TOAST_FAILED);
      return false;
    }
  }

  // 2. 获取互斥锁，等待超时时间设定为 10 秒（匹配 mDNS 多轮重试）
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
    ESP_LOGE(TAG, "Get mDNS mutex timeout");
    gs_toast_show("网络互斥锁超时", GS_TOAST_FAILED);
    return false;
  }

  bool ret = true;
  // 3. 检查缓存；无缓存时发起查询
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

// 主动清除 IP 缓存接口（网络请求失败时自动调用）
static void invalidate_server_ip_cache(void) {
  if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
    s_cached_ip[0] = '\0';
    xSemaphoreGive(s_mutex);
    ESP_LOGW(TAG, "mDNS IP cache invalidated.");
  }
}

#endif

/*---------------------------------------------------------------
 * HTTPS 配置
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
  if (!path) {
    path = "/";
  }

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

    int remain = ctx->max_len - 1 - ctx->total_len;
    if (remain <= 0) {
      ESP_LOGW(TAG, "Response buffer full, truncation occurred.");
      return ESP_FAIL;
    }

    int copy_len = (evt->data_len > remain) ? remain : evt->data_len;
    memcpy(ctx->buffer + ctx->total_len, evt->data, copy_len);
    ctx->total_len += copy_len;
    ctx->buffer[ctx->total_len] = '\0';
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
    gs_toast_show("创建HTTP客户端失败", GS_TOAST_FAILED);
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
    // 请求失败时清空 IP 缓存，防止 IP 变更导致后续请求一直报错
    invalidate_server_ip_cache();
#endif
    esp_http_client_cleanup(client);
    gs_toast_show("网络请求失败", GS_TOAST_FAILED);
    return false;
  }

  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (status >= 200 && status < 300) {
    return true;
  }

  ESP_LOGE(TAG, "HTTP status code error: %d | URL: %s", status, url);

  char err_toast[32];
  snprintf(err_toast, sizeof(err_toast), "服务器响应错误: %d", status);
  gs_toast_show(err_toast, GS_TOAST_FAILED);

  return false;
}

/*---------------------------------------------------------------
 * 公共 API 实现
 *--------------------------------------------------------------*/

bool http_helper_init(void) {
#ifdef CONFIG_HTTP_MODE_MDNS
  // 1. 保证互斥锁优先且安全创建
  if (s_mutex == NULL) {
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
      ESP_LOGE(TAG, "Failed to create s_mutex");
      return false;
    }
  }

  // 2. 同步初始化 mDNS 模块，确保连接前协议栈已就绪
  if (!mdns_helper_init("esp32-s3")) {
    ESP_LOGE(TAG, "Failed to initialize mDNS synchronously");
    return false;
  }

  return true;
#else
  ESP_LOGI(TAG, "HTTP Helper initialized in direct/HTTPS mode.");
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
    gs_toast_show("创建HTTP客户端失败", GS_TOAST_FAILED);
    return false;
  }

  esp_http_client_set_header(client, "Content-Type", "image/jpeg");

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
      gs_toast_show("文件上传失败", GS_TOAST_FAILED);
    }
  } else {
    ESP_LOGE(TAG, "PUT request failed: %s", esp_err_to_name(err));
    gs_toast_show("网络传输中断", GS_TOAST_FAILED);
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
    gs_toast_show("无法连接到服务器", GS_TOAST_FAILED);
    return false;
  }

  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (status >= 200 && status < 500) {
    ESP_LOGI(TAG, "Server reachable, status=%d", status);
    gs_toast_show("服务器连接正常", GS_TOAST_SUCCESS);
    return true;
  }

  ESP_LOGE(TAG, "Server unreachable, status=%d", status);
        gs_toast_show("服务器响应异常", GS_TOAST_FAILED);
  return false;
}