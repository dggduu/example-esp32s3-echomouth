#include "esp_bt.h"
#include "esp_err.h" // 必须置顶
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <string.h>
#include "ble_ota.h"
#include "page_ota.h" // UI 回调接口
#include "protocomm_ble.h"
#include "prov_sec2_gen.h" // 确保你的项目中包含这个生成安全 Key 的文件
#include "scheme_ble.h"

#define OTA_RINGBUF_SIZE 8192
#define OTA_TASK_SIZE 8192

static const char *TAG = "OTA_BACKEND";

// 静态资源句柄
static esp_ota_handle_t s_out_handle = 0;
static RingbufHandle_t s_ringbuf = NULL;
static TaskHandle_t s_ota_task_handle = NULL;
static SemaphoreHandle_t s_notify_sem = NULL;
static bool s_is_initialized = false;

// 数据接收回调：由 ble_ota 组件内部调用
void ota_recv_fw_cb(uint8_t *buf, uint32_t length) {
  if (s_ringbuf) {
    xRingbufferSend(s_ringbuf, (void *)buf, length, pdMS_TO_TICKS(100));
  }
}

// 系统事件回调
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == ESP_BLE_OTA_EVENT) {
    if (event_id == OTA_FILE_RCV) {
      // 注意：某些版本组件通过事件分发数据，这里将数据压入 ringbuffer
      ota_recv_fw_cb(event_data, 4096);
    }
  } else if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
    if (event_id == PROTOCOMM_TRANSPORT_BLE_CONNECTED) {
      ESP_LOGI(TAG, "BLE Connected");
      page_ota_notify_status("BLE Connected", 1);
    } else if (event_id == PROTOCOMM_TRANSPORT_BLE_DISCONNECTED) {
      ESP_LOGI(TAG, "BLE Disconnected");
      page_ota_notify_status("BLE Disconnected", 0);
    }
  }
}

// OTA 执行任务
static void ota_task(void *arg) {
  const esp_partition_t *update_partition =
      esp_ota_get_next_update_partition(NULL);
  uint32_t recv_len = 0;
  size_t item_size = 0;

  if (!update_partition) {
    ESP_LOGE(TAG, "No OTA partition found!");
    vTaskDelete(NULL);
  }

  if (esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &s_out_handle) !=
      ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed");
    page_ota_notify_status("OTA Begin Failed", 0);
    vTaskDelete(NULL);
  }

  s_notify_sem = xSemaphoreCreateBinary();
  xSemaphoreGive(s_notify_sem);

  ESP_LOGI(TAG, "OTA Task Started. Target partition: %s",
           update_partition->label);

  while (true) {
    uint8_t *data = (uint8_t *)xRingbufferReceive(s_ringbuf, &item_size,
                                                  pdMS_TO_TICKS(1000));

    if (data) {
      xSemaphoreTake(s_notify_sem, portMAX_DELAY);
      if (esp_ota_write(s_out_handle, (const void *)data, item_size) !=
          ESP_OK) {
        ESP_LOGE(TAG, "Flash write failed");
        vRingbufferReturnItem(s_ringbuf, (void *)data);
        xSemaphoreGive(s_notify_sem);
        break;
      }
      recv_len += item_size;
      vRingbufferReturnItem(s_ringbuf, (void *)data);

      // 更新 UI 进度
      uint32_t total_len = esp_ble_ota_get_fw_length();
      if (total_len > 0) {
        page_ota_notify_progress(recv_len, total_len);
      }

      if (recv_len >= total_len && total_len > 0) {
        xSemaphoreGive(s_notify_sem);
        break;
      }
      xSemaphoreGive(s_notify_sem);
    } else {
      // 超时未收到数据
      if (esp_ble_ota_get_fw_length() > 0 &&
          recv_len >= esp_ble_ota_get_fw_length())
        break;
    }
  }

  if (esp_ota_end(s_out_handle) == ESP_OK &&
      esp_ota_set_boot_partition(update_partition) == ESP_OK) {
    ESP_LOGI(TAG, "OTA Success, restarting...");
    page_ota_notify_status("Success! Restarting...", 2);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
  } else {
    ESP_LOGE(TAG, "OTA Finalization failed");
    page_ota_notify_status("Verify Failed", 0);
  }

  s_ota_task_handle = NULL;
  vTaskDelete(NULL);
}

bool ota_backend_init(void) {
  if (s_is_initialized)
    return true;

  s_ringbuf = xRingbufferCreate(OTA_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
  if (!s_ringbuf)
    return false;

  // 注册事件
  esp_event_handler_register(ESP_BLE_OTA_EVENT, ESP_EVENT_ANY_ID,
                             &event_handler, NULL);
  esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID,
                             &event_handler, NULL);

  // 初始化 BLE OTA 组件
  esp_ble_ota_config_t config = {
      .scheme = esp_ble_ota_scheme_ble,
      .scheme_event_handler = ESP_BLE_OTA_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM};
  ESP_ERROR_CHECK(esp_ble_ota_init(config));

  // 安全配置 (Security 2)
  esp_ble_ota_security2_params_t sec2_params = {0};
  prov_sec2_get_salt((const char **)&sec2_params.salt, &sec2_params.salt_len);
  prov_sec2_get_verifier((const char **)&sec2_params.verifier,
                         &sec2_params.verifier_len);

  ESP_ERROR_CHECK(esp_ble_ota_start(ESP_BLE_OTA_SECURITY_2, &sec2_params,
                                    "OTA_S3_DEV", NULL));

  xTaskCreate(ota_task, "ota_task", OTA_TASK_SIZE, NULL, 5, &s_ota_task_handle);

  s_is_initialized = true;
  return true;
}

void ota_backend_deinit(void) {
  if (!s_is_initialized)
    return;

  esp_ble_ota_stop();
  esp_event_handler_unregister(ESP_BLE_OTA_EVENT, ESP_EVENT_ANY_ID,
                               &event_handler);
  esp_event_handler_unregister(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID,
                               &event_handler);

  if (s_ota_task_handle) {
    vTaskDelete(s_ota_task_handle);
    s_ota_task_handle = NULL;
  }

  if (s_ringbuf) {
    vRingbufferDelete(s_ringbuf);
    s_ringbuf = NULL;
  }

  if (s_out_handle)
    esp_ota_abort(s_out_handle);

  s_is_initialized = false;
}