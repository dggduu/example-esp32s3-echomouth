#include "ota_backend.h"
#include "ble_ota.h"
#include "esp_bt.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "page_ota.h"
#include <string.h>

#define OTA_RINGBUF_SIZE 8192
#define OTA_TASK_STACK_SIZE 4096 // 减小，使用内部 RAM

static const char *TAG = "OTA_BACKEND";

static RingbufHandle_t s_ringbuf = NULL;
static TaskHandle_t s_ota_task = NULL;
static esp_ota_handle_t s_out_handle = 0;
SemaphoreHandle_t notify_sem = NULL;
static bool s_started = false;

bool ble_ota_ringbuf_init(uint32_t ringbuf_size) {
  s_ringbuf = xRingbufferCreate(ringbuf_size, RINGBUF_TYPE_BYTEBUF);
  return (s_ringbuf != NULL);
}

size_t write_to_ringbuf(const uint8_t *data, size_t size) {
  BaseType_t done =
      xRingbufferSend(s_ringbuf, (void *)data, size, portMAX_DELAY);
  return done ? size : 0;
}

void ota_recv_fw_cb(uint8_t *buf, uint32_t length) {
  write_to_ringbuf(buf, length);
}

void ota_task(void *arg) {
  esp_partition_t *partition_ptr = NULL;
  esp_partition_t partition;
  const esp_partition_t *next_partition = NULL;
  uint32_t recv_len = 0;
  uint8_t *data = NULL;
  size_t item_size = 0;
  ESP_LOGI(TAG, "OTA task started (internal RAM stack)");

  notify_sem = xSemaphoreCreateCounting(100, 0);
  xSemaphoreGive(notify_sem);

  partition_ptr = (esp_partition_t *)esp_ota_get_boot_partition();
  if (partition_ptr == NULL || partition_ptr->type != ESP_PARTITION_TYPE_APP) {
    ESP_LOGE(TAG, "Invalid boot partition");
    goto OTA_ERROR;
  }

  if (partition_ptr->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
    partition.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
  } else {
    next_partition = esp_ota_get_next_update_partition(partition_ptr);
    partition.subtype = next_partition ? next_partition->subtype
                                       : ESP_PARTITION_SUBTYPE_APP_OTA_0;
  }
  partition.type = ESP_PARTITION_TYPE_APP;
  partition_ptr = (esp_partition_t *)esp_partition_find_first(
      partition.type, partition.subtype, NULL);
  if (partition_ptr == NULL) {
    ESP_LOGE(TAG, "Partition not found");
    goto OTA_ERROR;
  }
  memcpy(&partition, partition_ptr, sizeof(esp_partition_t));
  if (esp_ota_begin(&partition, OTA_SIZE_UNKNOWN, &s_out_handle) != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed");
    goto OTA_ERROR;
  }

  ESP_LOGI(TAG, "Waiting for firmware data, expected size=%u",
           esp_ble_ota_get_fw_length());
  while (1) {
    data = (uint8_t *)xRingbufferReceive(s_ringbuf, &item_size, portMAX_DELAY);
    xSemaphoreTake(notify_sem, portMAX_DELAY);
    if (item_size) {
      if (esp_ota_write(s_out_handle, (const void *)data, item_size) !=
          ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed");
        goto OTA_ERROR;
      }
      recv_len += item_size;
      vRingbufferReturnItem(s_ringbuf, (void *)data);
      if (recv_len >= esp_ble_ota_get_fw_length()) {
        xSemaphoreGive(notify_sem);
        break;
      }
    }
    xSemaphoreGive(notify_sem);
  }

  if (esp_ota_end(s_out_handle) != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed");
    goto OTA_ERROR;
  }
  if (esp_ota_set_boot_partition(&partition) != ESP_OK) {
    ESP_LOGE(TAG, "set_boot_partition failed");
    goto OTA_ERROR;
  }
  vSemaphoreDelete(notify_sem);
  esp_restart();

OTA_ERROR:
  ESP_LOGE(TAG, "OTA failed");
  vTaskDelete(NULL);
}

bool ota_backend_init(void) {
  if (s_started)
    return true;

  esp_err_t ret;
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  if (!ble_ota_ringbuf_init(OTA_RINGBUF_SIZE)) {
    ESP_LOGE(TAG, "Ringbuf init failed");
    return false;
  }

  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
  ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
  ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

  if (esp_ble_ota_host_init() != ESP_OK) {
    ESP_LOGE(TAG, "BLE Host init failed");
    return false;
  }
  esp_ble_ota_recv_fw_data_callback(ota_recv_fw_cb);

  // OTA 任务必须使用内部 RAM（会写入 Flash）
  if (s_ota_task == NULL) {
    BaseType_t ret = xTaskCreate(ota_task, "ota_task", OTA_TASK_STACK_SIZE,
                                 NULL, 5, &s_ota_task);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create OTA task");
      return false;
    }
    ESP_LOGI(TAG, "OTA task created (internal SRAM, %d bytes)",
             OTA_TASK_STACK_SIZE);
  }

  page_ota_notify_status("Waiting for connection...", OTA_STATE_IDLE);
  s_started = true;
  return true;
}

void ota_backend_deinit(void) {
  if (!s_started)
    return;
  if (s_ota_task) {
    vTaskDelete(s_ota_task);
    s_ota_task = NULL;
  }
  if (s_ringbuf) {
    vRingbufferDelete(s_ringbuf);
    s_ringbuf = NULL;
  }
  esp_bt_controller_disable();
  esp_bt_controller_deinit();
  s_started = false;
}