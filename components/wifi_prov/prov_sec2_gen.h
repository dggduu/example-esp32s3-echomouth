#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// 获取 salt
esp_err_t prov_sec2_get_salt(const char **salt, uint16_t *salt_len);

// 获取 verifier
esp_err_t prov_sec2_get_verifier(const char **verifier, uint16_t *verifier_len);

#ifdef __cplusplus
}
#endif