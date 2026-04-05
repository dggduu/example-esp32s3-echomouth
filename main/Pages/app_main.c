/*
 * SPDX-FileCopyrightText: 2019-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

#include "ble_ota.h"
#include "esp_bt.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

#include "prov_sec_gen.h"

#define OTA_RINGBUF_SIZE 8192
#define OTA_TASK_SIZE 8192

static const char *TAG = "ESP_BLE_OTA";
static esp_ota_handle_t out_handle;
SemaphoreHandle_t notify_sem;

extern const char rsa_private_pem_start[] asm("_binary_private_pem_start");
extern const char rsa_private_pem_end[] asm("_binary_private_pem_end");
esp_decrypt_handle_t decrypt_handle;

#include "esp_netif.h"
#include "manager.h"
#include "scheme_ble.h"

static esp_err_t example_get_sec2_salt(const char **salt, uint16_t *salt_len) {
  return prov_sec2_get_salt(salt, salt_len);
}

static esp_err_t example_get_sec2_verifier(const char **verifier,
                                           uint16_t *verifier_len) {
  return prov_sec2_get_verifier(verifier, verifier_len);
}

/* Event handler for catching system events */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == ESP_BLE_OTA_EVENT) {
    switch (event_id) {
    case OTA_FILE_RCV:
      ESP_LOGD(TAG, "File received in appln layer :");
      ota_recv_fw_cb(event_data, 4096);
      break;
    default:
      break;
    }
  } else if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
    switch (event_id) {
    case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
      ESP_LOGI(TAG, "BLE transport: Connected!");
      break;
    case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
      ESP_LOGI(TAG, "BLE transport: Disconnected!");
      break;
    default:
      break;
    }
  }
}

static void get_device_service_name(char *service_name, size_t size) {
  char *svc_name = "OTA_123456";
  strlcpy(service_name, svc_name, size);
}

static RingbufHandle_t s_ringbuf = NULL;

bool ble_ota_ringbuf_init(uint32_t ringbuf_size) {
  s_ringbuf = xRingbufferCreate(ringbuf_size, RINGBUF_TYPE_BYTEBUF);
  if (s_ringbuf == NULL) {
    return false;
  }

  return true;
}

size_t write_to_ringbuf(const uint8_t *data, size_t size) {
  BaseType_t done =
      xRingbufferSend(s_ringbuf, (void *)data, size, (TickType_t)portMAX_DELAY);
  if (done) {
    return size;
  } else {
    return 0;
  }
}

void ota_task(void *arg) {
  esp_partition_t *partition_ptr = NULL;
  esp_partition_t partition;
  const esp_partition_t *next_partition = NULL;

  uint32_t recv_len = 0;
  uint8_t *data = NULL;
  size_t item_size = 0;
  ESP_LOGI(TAG, "ota_task start");

  notify_sem = xSemaphoreCreateCounting(100, 0);
  xSemaphoreGive(notify_sem);

  partition_ptr = (esp_partition_t *)esp_ota_get_boot_partition();
  if (partition_ptr == NULL) {
    ESP_LOGE(TAG, "boot partition NULL!\r\n");
    goto OTA_ERROR;
  }
  if (partition_ptr->type != ESP_PARTITION_TYPE_APP) {
    ESP_LOGE(TAG, "esp_current_partition->type != ESP_PARTITION_TYPE_APP\r\n");
    goto OTA_ERROR;
  }

  if (partition_ptr->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
    partition.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
  } else {
    next_partition = esp_ota_get_next_update_partition(partition_ptr);
    if (next_partition) {
      partition.subtype = next_partition->subtype;
    } else {
      partition.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
    }
  }
  partition.type = ESP_PARTITION_TYPE_APP;

  partition_ptr = (esp_partition_t *)esp_partition_find_first(
      partition.type, partition.subtype, NULL);
  if (partition_ptr == NULL) {
    ESP_LOGE(TAG, "partition NULL!\r\n");
    goto OTA_ERROR;
  }

  memcpy(&partition, partition_ptr, sizeof(esp_partition_t));
  if (esp_ota_begin(&partition, OTA_SIZE_UNKNOWN, &out_handle) != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed!\r\n");
    goto OTA_ERROR;
  }
  ESP_LOGI(TAG, "wait for data from ringbuf! fw_len = %u",
           esp_ble_ota_get_fw_length());
  /*deal with all receive packet*/
  for (;;) {
    data = (uint8_t *)xRingbufferReceive(s_ringbuf, &item_size,
                                         (TickType_t)portMAX_DELAY);

    xSemaphoreTake(notify_sem, portMAX_DELAY);

    ESP_LOGI(TAG, "recv: %u, recv_total:%" PRIu32 "\n", item_size,
             recv_len + item_size);

    if (item_size != 0) {
      if (esp_ota_write(out_handle, (const void *)data, item_size) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed!\r\n");
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

  if (esp_ota_end(out_handle) != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed!\r\n");
    goto OTA_ERROR;
  }
  if (esp_ota_set_boot_partition(&partition) != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed!\r\n");
    goto OTA_ERROR;
  }

  vSemaphoreDelete(notify_sem);
  esp_restart();

OTA_ERROR:
  ESP_LOGE(TAG, "OTA failed");
  vTaskDelete(NULL);
}

void ota_recv_fw_cb(uint8_t *buf, uint32_t length) {
  write_to_ringbuf(buf, length);
}

static void ota_task_init(void) {
  xTaskCreate(&ota_task, "ota_task", OTA_TASK_SIZE, NULL, 5, NULL);
  return;
}

void app_main(void) {
  esp_err_t ret;
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  // Initialize NVS
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  if (!ble_ota_ringbuf_init(OTA_RINGBUF_SIZE)) {
    ESP_LOGE(TAG, "%s init ringbuf fail", __func__);
    return;
  }

  /* Initialize TCP/IP */
  ESP_ERROR_CHECK(esp_netif_init());

  /* Initialize the event loop */
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  /* Register our event handler for Wi-Fi, IP and Provisioning related events */
  ESP_ERROR_CHECK(esp_event_handler_register(
      ESP_BLE_OTA_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(
      PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

  /* Configuration for the ota manager */
  esp_ble_ota_config_t config = {
      .scheme = esp_ble_ota_scheme_ble,

      .scheme_event_handler = ESP_BLE_OTA_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM};

  /* Initialize ota manager with the
   * configuration parameters set above */
  ESP_ERROR_CHECK(esp_ble_ota_init(config));

  char service_name[12];
  get_device_service_name(service_name, sizeof(service_name));
  esp_ble_ota_security_t security = ESP_BLE_OTA_SECURITY_2;

  /* The username must be the same one, which has been used in the generation of
   * salt and verifier */

  esp_ble_ota_security2_params_t sec2_params = {};

  ESP_ERROR_CHECK(
      example_get_sec2_salt(&sec2_params.salt, &sec2_params.salt_len));
  ESP_ERROR_CHECK(example_get_sec2_verifier(&sec2_params.verifier,
                                            &sec2_params.verifier_len));

  esp_ble_ota_security2_params_t *sec_params = &sec2_params;
  const char *service_key = NULL;

  ESP_ERROR_CHECK(esp_ble_ota_start(security, (const void *)sec_params,
                                    service_name, service_key));
  ota_task_init();
}
