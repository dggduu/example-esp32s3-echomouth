#include "efuse_helper.h"
#include "unity.h"
#include <string.h>

#ifdef CONFIG_EFUSE_VIRTUAL
TEST_CASE("eFuse: write & read UUID (128 bit)", "[efuse]") {
  const uint8_t test_uuid[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC,
                                 0xDD, 0xEE, 0xFF, 0x00};

  esp_err_t ret = efuse_helper_write_test_uuid(test_uuid);
  TEST_ASSERT_EQUAL(ESP_OK, ret);

  // 读取
  uint8_t read_uuid[16] = {0};
  ret = efuse_helper_read_uuid(read_uuid);
  TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Read UUID failed!");

  // 比较内容
  TEST_ASSERT_EQUAL_UINT8_ARRAY(test_uuid, read_uuid, 16);
}
#endif