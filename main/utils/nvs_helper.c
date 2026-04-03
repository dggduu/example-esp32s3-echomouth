#include "nvs_helper.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"


static const char *TAG = "NVS_HELPER";

esp_err_t nvs_helper_init(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase()); // 擦除异常分区
    ret = nvs_flash_init();             // 重新初始化
  }
  return ret;
}

esp_err_t nvs_helper_set_i32(const char *name_space, const char *key,
                             int32_t value) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(name_space, NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;

  err = nvs_set_i32(handle, key, value);
  if (err == ESP_OK)
    err = nvs_commit(handle); // 强制持久化

  nvs_close(handle);
  return err;
}

esp_err_t nvs_helper_get_i32(const char *name_space, const char *key,
                             int32_t *out_value) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(name_space, NVS_READONLY, &handle);
  if (err != ESP_OK)
    return err;

  err = nvs_get_i32(handle, key, out_value);
  nvs_close(handle);
  return err;
}

esp_err_t nvs_helper_set_string(const char *name_space, const char *key,
                                const char *value) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(name_space, NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;

  err = nvs_set_str(handle, key, value);
  if (err == ESP_OK)
    err = nvs_commit(handle);

  nvs_close(handle);
  return err;
}

esp_err_t nvs_helper_get_string(const char *name_space, const char *key,
                                char *out_value, size_t max_len) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(name_space, NVS_READONLY, &handle);
  if (err != ESP_OK)
    return err;

  size_t required_size;
  err = nvs_get_str(handle, key, NULL, &required_size); // 第一次获取长度
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  if (required_size > max_len) {
    nvs_close(handle);
    return ESP_ERR_INVALID_SIZE; // 缓冲区不足
  }

  err = nvs_get_str(handle, key, out_value, &required_size); // 第二次获取内容
  nvs_close(handle);
  return err;
}

esp_err_t nvs_helper_erase_key(const char *name_space, const char *key) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(name_space, NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;

  err = nvs_erase_key(handle, key);
  if (err == ESP_OK)
    err = nvs_commit(handle);

  nvs_close(handle);
  return err;
}