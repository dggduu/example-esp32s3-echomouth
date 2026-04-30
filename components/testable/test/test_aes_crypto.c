#include "aes_crypto_helper.h"
#include "esp_log.h"
#include "unity.h"
#include <string.h>

static void dump_hex(const char *label, const uint8_t *data, size_t len) {
  printf("%s: ", label);
  for (size_t i = 0; i < len; i++) {
    printf("%02X ", data[i]);
    if ((i + 1) % 16 == 0)
      printf("\n   ");
  }
  printf("\n");
}

static void test_setup_aes(void) {
  esp_err_t err = aes_crypto_register();
  TEST_ASSERT_EQUAL_HEX(ESP_OK, err);
}

static void test_teardown_aes(void) {
  esp_err_t err = aes_crypto_unregister();
  TEST_ASSERT_EQUAL_HEX(ESP_OK, err);
}

// NIST FIPS-197 附录 B 测试向量 (AES-128-ECB)
static const uint8_t key_128[] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae,
                                  0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88,
                                  0x09, 0xcf, 0x4f, 0x3c};
static const uint8_t plain_128[] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40,
                                    0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11,
                                    0x73, 0x93, 0x17, 0x2a};
static const uint8_t cipher_128[] = {0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a,
                                     0x36, 0x60, 0xa8, 0x9e, 0xca, 0xf3,
                                     0x24, 0x66, 0xef, 0x97};

TEST_CASE("AES-128-ECB: NIST FIPS-197 appendix B", "[aes_crypto]") {
  uint8_t output[16];
  uint8_t decrypted[16];

  test_setup_aes();

  TEST_ASSERT_EQUAL(ESP_OK,
                    aes_ecb_encrypt_block(key_128, 128, plain_128, output));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(cipher_128, output, 16);

  TEST_ASSERT_EQUAL(ESP_OK,
                    aes_ecb_decrypt_block(key_128, 128, output, decrypted));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(plain_128, decrypted, 16);

  test_teardown_aes();
}

TEST_CASE("AES-128-CBC: multi-block", "[aes_crypto]") {
  const uint8_t key[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                           0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
  const uint8_t iv[16] = {0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8,
                          0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2, 0xf1, 0xf0};
  const uint8_t plain[32] = "AES-CBC multi-block test!";
  uint8_t cipher[32];
  uint8_t decrypted[32];

  test_setup_aes();

  uint8_t iv_enc[16], iv_dec[16];
  memcpy(iv_enc, iv, 16);
  memcpy(iv_dec, iv, 16);
  size_t out_len;

  TEST_ASSERT_EQUAL(
      ESP_OK, aes_cbc_encrypt(key, 128, iv_enc, plain, 32, cipher, &out_len));
  TEST_ASSERT_EQUAL(32, out_len);

  TEST_ASSERT_EQUAL(ESP_OK, aes_cbc_decrypt(key, 128, iv_dec, cipher, 32,
                                            decrypted, &out_len));
  TEST_ASSERT_EQUAL(32, out_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(plain, decrypted, 32);

  test_teardown_aes();
}