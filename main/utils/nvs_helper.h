#ifndef NVS_HELPER_H
#define NVS_HELPER_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t nvs_helper_init(void);

esp_err_t nvs_helper_set_i32(const char *name_space, const char *key,
                             int32_t value);

esp_err_t nvs_helper_get_i32(const char *name_space, const char *key,
                             int32_t *out_value);

esp_err_t nvs_helper_set_string(const char *name_space, const char *key,
                                const char *value);

esp_err_t nvs_helper_get_string(const char *name_space, const char *key,
                                char *out_value, size_t max_len);

esp_err_t nvs_helper_erase_key(const char *name_space, const char *key);

// 快捷函数
int32_t nvs_helper_get_did();

int32_t nvs_helper_get_pid();

/**
 * @brief 存储派生设备密钥到 NVS
 * @param key 指向 16 字节密钥的指针
 * @return ESP_OK 成功，其他 NVS 错误码
 */
esp_err_t nvs_helper_set_device_key(const uint8_t *key);

/**
 * @brief 从 NVS 读取派生设备密钥
 * @param key_out 输出密钥的 16 字节缓冲区
 * @return ESP_OK 成功，其他 NVS 错误码
 */
esp_err_t nvs_helper_get_device_key(uint8_t *key_out);

#endif