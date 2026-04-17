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

int32_t nvs_helper_get_did();

int32_t nvs_helper_get_pid();

#endif