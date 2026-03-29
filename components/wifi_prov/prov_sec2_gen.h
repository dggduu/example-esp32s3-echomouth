#include "prov_sec2.h"
#include <esp_efuse.h>
#include <esp_log.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <string.h>

#define TAG "SEC2_GEN"

// ==================== 配置 ====================
#define SEC2_SALT_LEN 16
#define SEC2_USERNAME "wifiprov"
#define SEC2_PASSWORD "abcd1234"

// 生成 SALT
static void generate_salt(uint8_t *salt_out) {
  esp_fill_random(salt_out, SEC2_SALT_LEN);

  // 读取芯片唯一 ID
  uint8_t unique_id[8] = {0};
  esp_efuse_read_field_blob(ESP_EFUSE_UNIQUE_ID, unique_id, 64);

  // 随机数 + 唯一ID 做 SHA256 混合
  uint8_t buf[SEC2_SALT_LEN + 8];
  memcpy(buf, salt_out, SEC2_SALT_LEN);
  memcpy(buf + SEC2_SALT_LEN, unique_id, 8);

  uint8_t hash[32];
  mbedtls_sha256(buf, sizeof(buf), hash, 0);

  // 取前16字节作为最终盐
  memcpy(salt_out, hash, SEC2_SALT_LEN);

  ESP_LOGI(TAG, "salt 生成完成");
}

// 生成 SEC2 verifier
static void generate_verifier(const uint8_t *salt, const char *user,
                              const char *pass, uint8_t *verifier_out) {
  char up_buf[256];
  int up_len = snprintf(up_buf, sizeof(up_buf), "%s:%s", user, pass);

  uint8_t hmac_buf[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  (const unsigned char *)up_buf, up_len, salt, SEC2_SALT_LEN,
                  hmac_buf);

  memcpy(verifier_out, hmac_buf, 32);
  ESP_LOGI(TAG, "verifier 生成完成");
}

// 对外接口
// 获取 salt
esp_err_t prov_sec2_get_salt(const char **salt, uint16_t *salt_len) {
  static uint8_t g_salt[SEC2_SALT_LEN] = {0};
  static bool initialized = false;

  if (!initialized) {
    generate_salt(g_salt);
    initialized = true;
  }

  *salt = (const char *)g_salt;
  *salt_len = SEC2_SALT_LEN;
  return ESP_OK;
}

// 获取 verifier
esp_err_t prov_sec2_get_verifier(const char **verifier,
                                 uint16_t *verifier_len) {
  static uint8_t g_verifier[32] = {0};
  static bool initialized = false;

  const char *salt;
  uint16_t salt_len;
  prov_sec2_get_salt(&salt, &salt_len);

  if (!initialized) {
    generate_verifier((const uint8_t *)salt, SEC2_USERNAME, SEC2_PASSWORD,
                      g_verifier);
    initialized = true;
  }

  *verifier = (const char *)g_verifier;
  *verifier_len = 32;
  return ESP_OK;
}