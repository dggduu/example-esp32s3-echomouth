#include "aes_crypto_helper.h"
#include "esp_vfs_helper.h"
#include "unity.h"
#include <string.h>
#include <unistd.h>


static const char *TEST_PLAIN = "/storage/test_plain.txt";
static const char *TEST_ENC = "/storage/test_enc.aes";
static const char *TEST_DEC = "/storage/test_dec.txt";

static void test_cleanup_files(void) {
  unlink(TEST_PLAIN);
  unlink(TEST_ENC);
  unlink(TEST_DEC);
}

TEST_CASE("AES file: encrypt then decrypt", "[aes_crypto_file]") {
  const char *content = "Hello ESP32-S3 AES file encryption test!";
  size_t len = strlen(content);
  const uint8_t key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                           0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
  test_cleanup_files();

  // 创建明文文件
  FILE *f = fopen(TEST_PLAIN, "w");
  TEST_ASSERT_NOT_NULL(f);
  fwrite(content, 1, len, f);
  fclose(f);

  TEST_ASSERT_EQUAL(ESP_OK, aes_encrypt_file(TEST_PLAIN, TEST_ENC, key, 128));
  TEST_ASSERT_EQUAL(ESP_OK, aes_decrypt_file(TEST_ENC, TEST_DEC, key, 128));

  // 验证解密内容
  uint8_t *dec = NULL;
  size_t dec_len;
  TEST_ASSERT_EQUAL(ESP_OK,
                    vfs_helper_read_file_to_ram(TEST_DEC, &dec, &dec_len));
  TEST_ASSERT_EQUAL(len, dec_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(content, dec, len);
  free(dec);

  test_cleanup_files();
}