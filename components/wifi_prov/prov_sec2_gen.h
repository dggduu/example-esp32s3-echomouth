#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t prov_sec2_get_salt(const char **salt, uint16_t *salt_len);

esp_err_t prov_sec2_get_verifier(const char **verifier, uint16_t *verifier_len);

#ifdef __cplusplus
}
#endif