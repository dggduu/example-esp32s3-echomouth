// unit_main.c
#include "aes_crypto_helper.h"
#include "esp_log.h"
#include "esp_vfs_helper.h"
#include "unity.h"


static const char *TAG = "unit_main";

void app_main(void) {
  ESP_LOGI(TAG, "Initializing VFS and AES for tests...");
  vfs_helper_init("/storage"); // 全局初始化一次
  aes_crypto_register();

  int count = unity_get_test_count();
  ESP_LOGI(TAG, "Registered tests count: %d", count);
  unity_run_all_tests();
}