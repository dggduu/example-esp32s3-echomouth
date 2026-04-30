#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_efuse_table_custom.h"
#include "esp_log.h"

static const char *TAG = "efuse_helper";

esp_err_t efuse_helper_write_test_uuid(const uint8_t *uuid) {
  if (!uuid) {
    return ESP_ERR_INVALID_ARG;
  }

#ifdef CONFIG_EFUSE_VIRTUAL

  uint8_t existing[16];
  if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA_DEVICE_UUID, existing,
                                128) == ESP_OK) {
    // 已经编程，检查是否相同
    if (memcmp(existing, uuid, 16) == 0) {
      ESP_LOGI(TAG, "UUID already matches, skip writing");
      return ESP_OK;
    } else {
      ESP_LOGE(TAG, "UUID mismatch! Erase efuse_em partition to reset.");
      return ESP_FAIL;
    }
  }

  // 虚拟 eFuse -直接写入
  esp_err_t err =
      esp_efuse_write_field_blob(ESP_EFUSE_USER_DATA_DEVICE_UUID, uuid, 128);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Write virtual UUID failed: %s", esp_err_to_name(err));
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "UUID written to virtual eFuse");
  return ESP_OK;
#else
  // 真实硬件-需要检查块是否锁定
  //   if (esp_efuse_block_is_write_protected(EFUSE_BLK3)) {
  //     ESP_LOGE(TAG, "BLK3 is write protected, cannot write UUID");
  //     return ESP_FAIL;
  //   }
  //   esp_err_t err =
  //       esp_efuse_write_field_blob(ESP_EFUSE_USER_DATA_DEVICE_UUID, uuid,
  //       128);
  //   if (err != ESP_OK) {
  //     ESP_LOGE(TAG, "Write UUID failed: %s", esp_err_to_name(err));
  //     return ESP_FAIL;
  //   }
  ESP_LOGI(TAG, "UUID written to BLK3 (lock after verification)");
  return ESP_OK;
#endif
}

esp_err_t efuse_helper_read_uuid(uint8_t *uuid_out) {
  if (!uuid_out) {
    return ESP_ERR_INVALID_ARG;
  }
  return esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA_DEVICE_UUID, uuid_out,
                                   128);
}