// unit_main.c
#include "aes_crypto_helper.h"
#include "esp_log.h"
#include "esp_vfs_helper.h"
#include "unity.h"

#include "time_test_helper.h"

#include "freertos/FreeRTOS.h"

static const char *TAG = "unit_main";

void app_main(void) {
  ESP_LOGI(TAG, "Initializing VFS and AES for tests...");

  debug_start_heap_monitor(10 * 1000);

  vfs_helper_early_init();
  vfs_helper_init("/storage"); // 全局初始化一次
  aes_crypto_register();

  int count = unity_get_test_count();
  ESP_LOGI(TAG, "Registered tests count: %d", count);
  unity_run_all_tests();
}