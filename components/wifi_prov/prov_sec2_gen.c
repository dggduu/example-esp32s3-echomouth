#include "prov_sec2_gen.h"
#include "esp_log.h"
#include "esp_srp.h"
#include <string.h>

#define TAG "SEC2_GEN"
#define SEC2_USERNAME "wifiprov"
#define SEC2_PASSWORD "abcd1234"
#define SALT_LEN 16
#define VERIFIER_LEN 384

static char *g_salt = NULL;
static char *g_verifier = NULL;
static bool initialized = false;

static esp_err_t generate_salt_verifier(void) {
  esp_err_t err;
  int verifier_len_out = 0;

  err = esp_srp_gen_salt_verifier(SEC2_USERNAME, strlen(SEC2_USERNAME),
                                  SEC2_PASSWORD, strlen(SEC2_PASSWORD), &g_salt,
                                  SALT_LEN, &g_verifier, &verifier_len_out);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to generate salt/verifier: %d", err);
    return err;
  }

  if (verifier_len_out != VERIFIER_LEN) {
    ESP_LOGE(TAG, "Unexpected verifier length: %d (expected %d)",
             verifier_len_out, VERIFIER_LEN);
    free(g_salt);
    free(g_verifier);
    g_salt = NULL;
    g_verifier = NULL;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Salt/Verifier generated (salt_len=%d, verifier_len=%d)",
           SALT_LEN, verifier_len_out);
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
  *salt_len = SALT_LEN;
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
  *verifier_len = VERIFIER_LEN;
  return ESP_OK;
}