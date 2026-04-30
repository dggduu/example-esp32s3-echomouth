#include "aes_crypto_helper.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/aes.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// 引用 vfs_helper 的头文件
#include "esp_vfs_helper.h"

static const char *TAG = "aes_crypto";
static SemaphoreHandle_t s_mutex = NULL;
static int s_ref_count = 0;

// 确保互斥锁已创建
static esp_err_t ensure_mutex(void) {
  if (!s_mutex) {
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
      ESP_LOGE(TAG, "Failed to create mutex");
      return ESP_ERR_NO_MEM;
    }
  }
  return ESP_OK;
}

esp_err_t aes_crypto_register(void) {
  esp_err_t ret = ensure_mutex();
  if (ret != ESP_OK)
    return ret;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_ref_count == 0) {
    ESP_LOGI(TAG, "AES hardware registered (first user)");
  }
  s_ref_count++;
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}

esp_err_t aes_crypto_unregister(void) {
  if (!s_mutex)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_ref_count > 0) {
    s_ref_count--;
    if (s_ref_count == 0) {
      ESP_LOGI(TAG, "AES hardware unregistered (last user)");
    }
  }
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}

// 内部通用CBC加解密（要求长度是16的倍数）
static esp_err_t aes_cbc_crypt(const uint8_t *key, size_t key_bits, uint8_t *iv,
                               const uint8_t *input, size_t in_len,
                               uint8_t *output, bool encrypt) {
  if (!key || !iv || !input || !output || in_len == 0 || (in_len % 16) != 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (key_bits != 128 && key_bits != 192 && key_bits != 256) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  int ret;
  if (encrypt) {
    ret = mbedtls_aes_setkey_enc(&ctx, key, key_bits);
  } else {
    ret = mbedtls_aes_setkey_dec(&ctx, key, key_bits);
  }
  if (ret != 0) {
    mbedtls_aes_free(&ctx);
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_ARG;
  }

  ret = mbedtls_aes_crypt_cbc(
      &ctx, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, in_len, iv,
      input, output);
  mbedtls_aes_free(&ctx);
  xSemaphoreGive(s_mutex);
  return (ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t aes_cbc_encrypt(const uint8_t *key, size_t key_bits, uint8_t *iv,
                          const uint8_t *input, size_t in_len, uint8_t *output,
                          size_t *out_len) {
  esp_err_t err = aes_cbc_crypt(key, key_bits, iv, input, in_len, output, true);
  if (err == ESP_OK && out_len)
    *out_len = in_len;
  return err;
}

esp_err_t aes_cbc_decrypt(const uint8_t *key, size_t key_bits, uint8_t *iv,
                          const uint8_t *input, size_t in_len, uint8_t *output,
                          size_t *out_len) {
  esp_err_t err =
      aes_cbc_crypt(key, key_bits, iv, input, in_len, output, false);
  if (err == ESP_OK && out_len)
    *out_len = in_len;
  return err;
}

// ECB 单块加解密
static esp_err_t aes_ecb_block_crypt(const uint8_t *key, size_t key_bits,
                                     const uint8_t *input, uint8_t *output,
                                     bool encrypt) {
  if (!key || !input || !output)
    return ESP_ERR_INVALID_ARG;
  if (key_bits != 128 && key_bits != 192 && key_bits != 256) {
    return ESP_ERR_INVALID_ARG;
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  int ret;
  if (encrypt) {
    ret = mbedtls_aes_setkey_enc(&ctx, key, key_bits);
  } else {
    ret = mbedtls_aes_setkey_dec(&ctx, key, key_bits);
  }
  if (ret != 0) {
    mbedtls_aes_free(&ctx);
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_ARG;
  }
  ret = mbedtls_aes_crypt_ecb(
      &ctx, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, input, output);
  mbedtls_aes_free(&ctx);
  xSemaphoreGive(s_mutex);
  return (ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t aes_ecb_encrypt_block(const uint8_t *key, size_t key_bits,
                                const uint8_t *input, uint8_t *output) {
  return aes_ecb_block_crypt(key, key_bits, input, output, true);
}

esp_err_t aes_ecb_decrypt_block(const uint8_t *key, size_t key_bits,
                                const uint8_t *input, uint8_t *output) {
  return aes_ecb_block_crypt(key, key_bits, input, output, false);
}

// PKCS#7 填充
static size_t pkcs7_pad(const uint8_t *in, size_t in_len, uint8_t *out,
                        size_t out_capacity) {
  size_t pad_len = 16 - (in_len % 16);
  if (in_len + pad_len > out_capacity)
    return 0;
  memcpy(out, in, in_len);
  for (size_t i = in_len; i < in_len + pad_len; i++) {
    out[i] = (uint8_t)pad_len;
  }
  return in_len + pad_len;
}

static size_t pkcs7_unpad(const uint8_t *in, size_t in_len, uint8_t *out,
                          size_t out_capacity) {
  if (in_len == 0 || (in_len % 16) != 0)
    return 0;
  uint8_t pad_len = in[in_len - 1];
  if (pad_len == 0 || pad_len > 16)
    return 0;
  for (size_t i = in_len - pad_len; i < in_len; i++) {
    if (in[i] != pad_len)
      return 0;
  }
  size_t plain_len = in_len - pad_len;
  if (plain_len > out_capacity)
    return 0;
  memcpy(out, in, plain_len);
  return plain_len;
}

// 加密文件
esp_err_t aes_encrypt_file(const char *in_path, const char *out_path,
                           const uint8_t *key, size_t key_bits) {
  if (!in_path || !out_path || !key)
    return ESP_ERR_INVALID_ARG;
  ESP_LOGI(TAG, "Encrypting file: %s -> %s", in_path, out_path);

  uint8_t *plain_buf = NULL;
  size_t plain_len = 0;
  esp_err_t err = vfs_helper_read_file_to_ram(in_path, &plain_buf, &plain_len);
  if (err != ESP_OK)
    return err;

  uint8_t iv[16];
  esp_fill_random(iv, sizeof(iv));

  size_t padded_len = ((plain_len + 15) / 16) * 16;
  uint8_t *padded_buf = heap_caps_malloc(padded_len, MALLOC_CAP_INTERNAL);
  if (!padded_buf) {
    free(plain_buf);
    return ESP_ERR_NO_MEM;
  }
  size_t actual_padded =
      pkcs7_pad(plain_buf, plain_len, padded_buf, padded_len);
  if (actual_padded != padded_len) {
    free(plain_buf);
    free(padded_buf);
    return ESP_ERR_INVALID_SIZE;
  }
  free(plain_buf);

  uint8_t *cipher_buf = heap_caps_malloc(padded_len, MALLOC_CAP_INTERNAL);
  if (!cipher_buf) {
    free(padded_buf);
    return ESP_ERR_NO_MEM;
  }
  uint8_t iv_work[16];
  memcpy(iv_work, iv, 16);
  err = aes_cbc_encrypt(key, key_bits, iv_work, padded_buf, padded_len,
                        cipher_buf, NULL);
  free(padded_buf);
  if (err != ESP_OK) {
    free(cipher_buf);
    return err;
  }

  size_t out_len = sizeof(iv) + padded_len;
  uint8_t *out_buf = heap_caps_malloc(out_len, MALLOC_CAP_INTERNAL);
  if (!out_buf) {
    free(cipher_buf);
    return ESP_ERR_NO_MEM;
  }
  memcpy(out_buf, iv, sizeof(iv));
  memcpy(out_buf + sizeof(iv), cipher_buf, padded_len);
  free(cipher_buf);

  err = vfs_helper_write_file_from_ram(out_path, out_buf, out_len);
  free(out_buf);
  return err;
}

// 解密文件
esp_err_t aes_decrypt_file(const char *in_path, const char *out_path,
                           const uint8_t *key, size_t key_bits) {
  if (!in_path || !out_path || !key)
    return ESP_ERR_INVALID_ARG;
  ESP_LOGI(TAG, "Decrypting file: %s -> %s", in_path, out_path);

  uint8_t *enc_buf = NULL;
  size_t enc_len = 0;
  esp_err_t err = vfs_helper_read_file_to_ram(in_path, &enc_buf, &enc_len);
  if (err != ESP_OK)
    return err;
  if (enc_len < 16) {
    free(enc_buf);
    return ESP_ERR_INVALID_SIZE;
  }

  uint8_t iv[16];
  memcpy(iv, enc_buf, 16);
  size_t cipher_len = enc_len - 16;
  if (cipher_len % 16 != 0) {
    free(enc_buf);
    return ESP_ERR_INVALID_SIZE;
  }

  uint8_t *decrypted_buf = heap_caps_malloc(cipher_len, MALLOC_CAP_INTERNAL);
  if (!decrypted_buf) {
    free(enc_buf);
    return ESP_ERR_NO_MEM;
  }
  uint8_t iv_work[16];
  memcpy(iv_work, iv, 16);
  err = aes_cbc_decrypt(key, key_bits, iv_work, enc_buf + 16, cipher_len,
                        decrypted_buf, NULL);
  free(enc_buf);
  if (err != ESP_OK) {
    free(decrypted_buf);
    return err;
  }

  uint8_t *plain_buf = heap_caps_malloc(cipher_len, MALLOC_CAP_INTERNAL);
  if (!plain_buf) {
    free(decrypted_buf);
    return ESP_ERR_NO_MEM;
  }
  size_t plain_len =
      pkcs7_unpad(decrypted_buf, cipher_len, plain_buf, cipher_len);
  if (plain_len == 0) {
    free(decrypted_buf);
    free(plain_buf);
    return ESP_ERR_INVALID_RESPONSE;
  }
  free(decrypted_buf);

  err = vfs_helper_write_file_from_ram(out_path, plain_buf, plain_len);
  free(plain_buf);
  return err;
}