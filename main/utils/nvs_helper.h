#ifndef NVS_HELPER_H
#define NVS_HELPER_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 初始化 NVS 分区
 * @return esp_err_t ESP_OK 表示成功
 */
esp_err_t nvs_helper_init(void);

/**
 * @brief 存储整形数值 (I32)
 */
esp_err_t nvs_helper_set_i32(const char *name_space, const char *key,
                             int32_t value);

/**
 * @brief 读取整形数值 (I32)
 */
esp_err_t nvs_helper_get_i32(const char *name_space, const char *key,
                             int32_t *out_value);

/**
 * @brief 存储字符串
 */
esp_err_t nvs_helper_set_string(const char *name_space, const char *key,
                                const char *value);

/**
 * @brief 读取字符串 (自动分配内存或检查缓冲区)
 * @param out_value 建议传入预分配的 buffer
 * @param max_len buffer 最大长度
 */
esp_err_t nvs_helper_get_string(const char *name_space, const char *key,
                                char *out_value, size_t max_len);

esp_err_t nvs_helper_erase_key(const char *name_space, const char *key);

int32_t nvs_helper_get_did();

int32_t nvs_helper_get_pid();

#endif