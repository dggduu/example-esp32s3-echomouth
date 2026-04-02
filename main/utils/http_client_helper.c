#include "http_client_helper.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "HTTP";

bool http_get_json(const char *url, char *response, int max_len) {
  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_GET,
      .timeout_ms = 5000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client)
    return false;

  esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    esp_http_client_cleanup(client);
    return false;
  }

  int len = esp_http_client_read(client, response, max_len - 1);
  if (len >= 0)
    response[len] = 0;

  esp_http_client_cleanup(client);
  return true;
}

bool http_put_binary(const char *url, uint8_t *data, int len) {
  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_PUT,
      .timeout_ms = 10000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client)
    return false;

  esp_http_client_set_header(client, "Content-Type", "image/jpeg");
  esp_http_client_set_post_field(client, (const char *)data, len);

  esp_err_t err = esp_http_client_perform(client);

  esp_http_client_cleanup(client);

  return err == ESP_OK;
}

bool http_post_json(const char *url, const char *json) {
  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_POST,
      .timeout_ms = 5000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client)
    return false;

  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, json, strlen(json));

  esp_err_t err = esp_http_client_perform(client);

  esp_http_client_cleanup(client);

  return err == ESP_OK;
}
