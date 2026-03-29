#include "prov_sec2_gen.h"
#include "esp_log.h"
#include "esp_srp.h"
#include <string.h>

#define TAG "SEC2_GEN"
#define SEC2_SALT_LEN 16
#define SEC2_USERNAME "wifiprov"
#define SEC2_PASSWORD "abcd1234"

static char *g_salt = NULL;
static int g_salt_len = 0;
static char *g_verifier = NULL;
static int g_verifier_len = 0;
static bool initialized = false;

static esp_err_t generate_salt_verifier(void) {
  esp_err_t err =
      esp_srp_gen_salt_verifier(SEC2_USERNAME, strlen(SEC2_USERNAME),
                                SEC2_PASSWORD, strlen(SEC2_PASSWORD), &g_salt,
                                SEC2_SALT_LEN, &g_verifier, &g_verifier_len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to generate salt/verifier: %d", err);
    return err;
  }
  g_salt_len =
      SEC2_SALT_LEN; // API 保证返回的 salt 长度等于传入的 SEC2_SALT_LEN
  ESP_LOGI(TAG, "Salt/Verifier generated (salt len=%d, verifier len=%d)",
           g_salt_len, g_verifier_len);
  return ESP_OK;
}

esp_err_t prov_sec2_get_salt(const char **salt, uint16_t *salt_len) {
  if (!initialized) {
    if (generate_salt_verifier() != ESP_OK) {
      return ESP_FAIL;
    }
    initialized = true;
  }
  *salt = g_salt;
  *salt_len = g_salt_len;
  return ESP_OK;
}

esp_err_t prov_sec2_get_verifier(const char **verifier,
                                 uint16_t *verifier_len) {
  if (!initialized) {
    if (generate_salt_verifier() != ESP_OK) {
      return ESP_FAIL;
    }
    initialized = true;
  }
  *verifier = g_verifier;
  *verifier_len = g_verifier_len;
  return ESP_OK;
}