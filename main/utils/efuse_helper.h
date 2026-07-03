#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t efuse_helper_write_test_uuid(const uint8_t *uuid); // 写入128位UUID
esp_err_t efuse_helper_read_uuid(uint8_t *uuid_out);         // 读出128位UUID

#ifdef __cplusplus
}
#endif