#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>

#include "cJSON.h"
#include "nvs_flash.h"

#include "prov_qr.h"
#include "prov_sec2_gen.h"
#include "wifi_prov.h"
static const char *TAG = "wifi_prov";
#define CONFIG_EXAMPLE_PROV_SECURITY_VERSION_2
#define CONFIG_EXAMPLE_PROV_SEC2_DEV_MODE
#define CONFIG_PROV_MGR_CONNECTION_CNT 5

#define EXAMPLE_PROV_SEC2_USERNAME "wifiprov"
#define EXAMPLE_PROV_SEC2_PWD "abcd1234"

static esp_err_t example_get_sec2_salt(const char **salt, uint16_t *salt_len) {
  ESP_LOGI(TAG, "Development mode: dynamically generating salt");
  return prov_sec2_get_salt(salt, salt_len); // 返回 16 字节的 salt
}

static esp_err_t example_get_sec2_verifier(const char **verifier,
                                           uint16_t *verifier_len) {
  ESP_LOGI(TAG, "Development mode: dynamically generating verifier");
  return prov_sec2_get_verifier(verifier, verifier_len);
}

const int WIFI_CONNECTED_EVENT = BIT0;
static EventGroupHandle_t wifi_event_group;

#define PROV_QR_VERSION "v1"
#define PROV_TRANSPORT_BLE "ble"

/* 自定义事件回调 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_PROV_EVENT) {
    switch (event_id) {
    case WIFI_PROV_START:
      ESP_LOGI(TAG, "Provisioning started");
      break;
    case WIFI_PROV_CRED_RECV: {
      wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
      ESP_LOGI(TAG,
               "Received Wi-Fi credentials"
               "\n\tSSID     : %s\n\tPassword : %s",
               (const char *)wifi_sta_cfg->ssid,
               (const char *)wifi_sta_cfg->password);
      break;
    }
    case WIFI_PROV_CRED_FAIL: {
      prov_qr_set_status(GS_QR_FAILED);

      wifi_prov_sta_fail_reason_t *reason =
          (wifi_prov_sta_fail_reason_t *)event_data;
      ESP_LOGE(TAG,
               "Provisioning failed!\n\tReason : %s"
               "\n\tPlease reset to factory and retry provisioning",
               (*reason == WIFI_PROV_STA_AUTH_ERROR)
                   ? "Wi-Fi station authentication failed"
                   : "Wi-Fi access-point not found");
      wifi_prov_mgr_reset_sm_state_on_failure();
      break;
    }
    case WIFI_PROV_CRED_SUCCESS:
      ESP_LOGI(TAG, "Provisioning successful");

      prov_qr_set_status(GS_QR_SUCCESS);
      break;
    case WIFI_PROV_END:
      wifi_prov_mgr_deinit();
      break;
    default:
      break;
    }
  } else if (event_base == WIFI_EVENT) {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
      esp_wifi_connect();
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
      esp_wifi_connect();
      break;
    default:
      break;
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Connected with IP Address:" IPSTR,
             IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
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
  } else if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
    switch (event_id) {
    case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
      ESP_LOGI(TAG, "Secured session established!");
      break;
    case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
      ESP_LOGE(TAG, "Received invalid security parameters for establishing "
                    "secure session!");

      prov_qr_set_status(GS_QR_FAILED);
      break;
    case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
      ESP_LOGE(TAG, "Received incorrect username and/or PoP for establishing "
                    "secure session!");

      prov_qr_set_status(GS_QR_FAILED);
      break;
    default:
      break;
    }
  }
}

static void wifi_init_sta(void) {
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
}

// 自定义显示设备名称
static void get_device_service_name(char *service_name, size_t max) {
  uint8_t eth_mac[6];
  const char *ssid_prefix = "PROV_";
  esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
  snprintf(service_name, max, "%s%02X%02X%02X", ssid_prefix, eth_mac[3],
           eth_mac[4], eth_mac[5]);
}

// 接收家长端的数据
esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf,
                                   ssize_t inlen, uint8_t **outbuf,
                                   ssize_t *outlen, void *priv_data) {
  esp_err_t ret = ESP_OK;
  const char *response = "SUCCESS";

  if (inbuf && inlen > 0) {
    ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);

    // 解析 JSON
    cJSON *root = cJSON_Parse((const char *)inbuf);
    if (root == NULL) {
      ESP_LOGE(TAG, "JSON parse error");
      response = "FAILED";
    } else {
      cJSON *deviceId_obj = cJSON_GetObjectItem(root, "dId");
      cJSON *parentId_obj = cJSON_GetObjectItem(root, "pId");

      if (deviceId_obj && cJSON_IsString(deviceId_obj) && parentId_obj &&
          cJSON_IsString(parentId_obj)) {

        const char *device_id = deviceId_obj->valuestring;
        const char *parent_id = parentId_obj->valuestring;

        ESP_LOGI(TAG, "deviceId: %s, parentId: %s", device_id, parent_id);

        // 存储到 NVS
        nvs_handle_t nvs_handle;
        ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
        if (ret == ESP_OK) {
          ret = nvs_set_str(nvs_handle, "device_id", device_id);
          if (ret != ESP_OK)
            ESP_LOGE(TAG, "Failed to set device_id: %s", esp_err_to_name(ret));
          ret = nvs_set_str(nvs_handle, "parent_id", parent_id);
          if (ret != ESP_OK)
            ESP_LOGE(TAG, "Failed to set parent_id: %s", esp_err_to_name(ret));
          nvs_commit(nvs_handle);
          nvs_close(nvs_handle);
        } else {
          ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
          response = "FAILED";
        }
      } else {
        ESP_LOGE(TAG, "Missing or invalid deviceId/parentId");
        response = "FAILED";
      }
      cJSON_Delete(root);
    }
  } else {
    ESP_LOGW(TAG, "Empty data received");
    response = "FAILED";
  }

  *outbuf = (uint8_t *)strdup(response);
  if (*outbuf == NULL) {
    ESP_LOGE(TAG, "System out of memory");
    return ESP_ERR_NO_MEM;
  }
  *outlen = strlen(response) + 1;
  return ESP_OK;
}

// 自定义wifi_prov 事件回调
void wifi_prov_app_callback(void *user_data, wifi_prov_cb_event_t event,
                            void *event_data) {
  switch (event) {
  case WIFI_PROV_SET_STA_CONFIG: {
    wifi_config_t *wifi_config = (wifi_config_t *)event_data;
    (void)wifi_config;
    break;
  }
  default:
    break;
  }
}

const wifi_prov_event_handler_t wifi_prov_event_handler = {
    .event_cb = wifi_prov_app_callback,
    .user_data = NULL,
};

void wifi_prov_reset_state() {
  wifi_prov_mgr_reset_sm_state_for_reprovision();
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true,
                      portMAX_DELAY);
}

void wifi_prov_nvs_init() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());

    ESP_ERROR_CHECK(nvs_flash_init());
  }
  ESP_ERROR_CHECK(esp_netif_init());
}

static void provisioning_task(void *arg) {
  // 检测是否已配网
  bool provisioned = false;
  ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

  if (!provisioned) {
    ESP_LOGI(TAG, "Starting provisioning");

    // 生成设备名称
    char service_name[12];
    get_device_service_name(service_name, sizeof(service_name));

    wifi_prov_security_t security = WIFI_PROV_SECURITY_2;
    const char *username = EXAMPLE_PROV_SEC2_USERNAME;
    const char *pop = EXAMPLE_PROV_SEC2_PWD;

    // 获取 salt/verifier
    wifi_prov_security2_params_t sec2_params = {};
    ESP_ERROR_CHECK(
        example_get_sec2_salt(&sec2_params.salt, &sec2_params.salt_len));
    ESP_ERROR_CHECK(example_get_sec2_verifier(&sec2_params.verifier,
                                              &sec2_params.verifier_len));

    // 自定义 UUID
    uint8_t custom_service_uuid[] = {
        0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
    };
    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

    // 创建自定义 endpoint
    wifi_prov_mgr_endpoint_create("custom-data");

    // 启动 provisioning
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
        security, (const void *)&sec2_params, service_name, NULL));

    wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler,
                                    NULL);

    // 异步显示二维码
    wifi_prov_print_qr(service_name, username, pop, PROV_TRANSPORT_BLE);
  } else {
    ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");
    wifi_prov_mgr_deinit();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &event_handler, NULL));
    wifi_init_sta();
  }

  // 等待 Wi-Fi 连接
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true,
                      portMAX_DELAY);

  // 任务结束，自行删除
  vTaskDelete(NULL);
}

esp_err_t wifi_prov_init(void) {
  static bool initialized = false;

  if (!initialized) {
    // 1. NVS 和网络接口初始化
    wifi_prov_nvs_init();

    // 2. 事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. 创建事件组
    wifi_event_group = xEventGroupCreate();

    // 4. 注册事件处理器
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT,
                                               ESP_EVENT_ANY_ID, &event_handler,
                                               NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &event_handler, NULL));

    // 5. 创建 WiFi station 网络接口
    esp_netif_create_default_wifi_sta();

    // 6. 初始化 WiFi 驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 7. 初始化 provisioning manager
    wifi_prov_mgr_config_t config = {
        .wifi_prov_conn_cfg =
            {
                .wifi_conn_attempts = CONFIG_PROV_MGR_CONNECTION_CNT,
            },
        .scheme = wifi_prov_scheme_ble,
        .app_event_handler = wifi_prov_event_handler,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM};
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    initialized = true;
  }

  // 启动 provisioning 任务（如果已配网，该任务会直接连接 WiFi）
  xTaskCreate(provisioning_task, "wifi_prov", 8192, NULL, 5, NULL);

  return ESP_OK;
}