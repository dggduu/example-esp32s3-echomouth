#include "net_adapter.h"
#include "chat_service.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gs_portal.h"
#include "protocol.h"
#include <string.h>

#include "protocol_debug.h"

#define PENDING_QUEUE_SIZE 10
#define PENDING_BUF_SIZE 1024
#define RX_RINGBUF_SIZE (1024 * 8)

#define NET_TX_STACK_SIZE 8192
#define NET_RX_STACK_SIZE 8192
#define NET_RECONN_STACK_SIZE 6144

static const char *TAG = "NET_ADAPTER";

static esp_websocket_client_handle_t g_ws_client = NULL;
static volatile bool g_ws_ready = false;
static net_config_t g_active_cfg;
static net_status_callback_t g_user_status_cb = NULL;

static TaskHandle_t g_tx_task_handle = NULL;
static TaskHandle_t g_rx_task_handle = NULL;
static TaskHandle_t g_reconnect_task_handle = NULL;

static StaticTask_t s_tx_task_tcb;
static StackType_t *s_tx_task_stack = NULL;
static StaticTask_t s_rx_task_tcb;
static StackType_t *s_rx_task_stack = NULL;
static StaticTask_t s_reconn_task_tcb;
static StackType_t *s_reconn_task_stack = NULL;

typedef struct {
  uint8_t *data;
  size_t len;
} tx_msg_t;

static QueueHandle_t g_tx_queue = NULL;
static RingbufHandle_t g_rx_ringbuf = NULL;
static SemaphoreHandle_t g_ws_mutex = NULL;

static void ws_event_handler(void *handler_args, esp_event_base_t base,
                             int32_t event_id, void *event_data);
static void notify_status(net_status_t status, const char *msg);
static void default_status_callback(net_status_t status, const char *msg);
static void net_tx_task(void *pvParameters);
static void net_rx_task(void *pvParameters);
static void reconnect_task(void *pvParameters);

/* ---------- 状态通知 ---------- */
static void default_status_callback(net_status_t status, const char *msg) {
  switch (status) {
  case NET_STATUS_CONNECTED:
    gs_toast_show(msg, GS_TOAST_SUCCESS);
    break;
  case NET_STATUS_DISCONNECTED:
    gs_toast_show(msg, GS_TOAST_FAILED);
    break;
  case NET_STATUS_RECONNECTING:
    gs_toast_show(msg, GS_TOAST_INFO);
    break;
  default:
    break;
  }
}

static void notify_status(net_status_t status, const char *msg) {
  if (g_user_status_cb) {
    g_user_status_cb(status, msg);
  } else {
    default_status_callback(status, msg);
  }
}

void net_set_status_callback(net_status_callback_t cb) {
  g_user_status_cb = cb;
}

void net_reset_status_callback(void) { g_user_status_cb = NULL; }

/* ---------- WebSocket 事件 ---------- */
static void ws_event_handler(void *handler_args, esp_event_base_t base,
                             int32_t event_id, void *event_data) {
  esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

  switch (event_id) {
  case WEBSOCKET_EVENT_CONNECTED:
    ESP_LOGI(TAG, "WebSocket connected");
    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    g_ws_ready = true;
    xSemaphoreGive(g_ws_mutex);

    notify_status(NET_STATUS_CONNECTED, "已连接到服务器");
    break;

  case WEBSOCKET_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "WebSocket disconnected");
    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    g_ws_ready = false;
    xSemaphoreGive(g_ws_mutex);
    notify_status(NET_STATUS_DISCONNECTED, "连接已断开");
    break;

  case WEBSOCKET_EVENT_DATA:
    if (data->op_code == WS_TRANSPORT_OPCODES_BINARY && data->data_ptr &&
        data->data_len > 0) {
      if (g_rx_ringbuf) {
        xRingbufferSend(g_rx_ringbuf, data->data_ptr, data->data_len,
                        pdMS_TO_TICKS(100));
      }
    }
    break;

  case WEBSOCKET_EVENT_ERROR:
    ESP_LOGE(TAG, "WebSocket error");
    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    g_ws_ready = false;
    xSemaphoreGive(g_ws_mutex);
    break;

  default:
    break;
  }
}

/* ---------- 发送任务 ---------- */
static void net_tx_task(void *pvParameters) {
  tx_msg_t msg;
  ESP_LOGI(TAG, "net_tx_task started");
  while (1) {
    if (xQueueReceive(g_tx_queue, &msg, portMAX_DELAY) == pdPASS) {
      bool ready;
      xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
      ready = g_ws_ready && g_ws_client &&
              esp_websocket_client_is_connected(g_ws_client);
      xSemaphoreGive(g_ws_mutex);

      if (ready) {
        esp_websocket_client_send_bin(g_ws_client, (const char *)msg.data,
                                      msg.len, pdMS_TO_TICKS(1000));
      } else {
        ESP_LOGW(TAG, "WS offline, drop packet len: %d", msg.len);
      }
      free(msg.data);
    }
  }
}

/* ---------- 接收解包任务 ---------- */
static void net_rx_task(void *pvParameters) {
  static uint8_t packet_buf[1024];
  int rx_idx = 0;
  ESP_LOGI(TAG, "net_rx_task started");

  while (1) {
    size_t item_size;
    uint8_t *data =
        (uint8_t *)xRingbufferReceive(g_rx_ringbuf, &item_size, portMAX_DELAY);
    if (data) {
      for (size_t i = 0; i < item_size; i++) {
        if (rx_idx == 0 && data[i] != STX)
          continue;
        if (rx_idx < sizeof(packet_buf)) {
          packet_buf[rx_idx++] = data[i];
        }

        if (rx_idx >= 11) {
          uint16_t payload_len;
          memcpy(&payload_len, &packet_buf[8], 2);
          int total_len = 10 + payload_len + 1;

          if (rx_idx == total_len) {
            protocol_packet_t pkt;
            if (decode_packet(packet_buf, total_len, &pkt)) {
              ESP_LOGI(TAG, "packet received type:%d", pkt.type);
              print_protocol_packet(&pkt);

              if (pkt.type == TYPE_SESSION_RANDOM) {
                uint16_t payload_len;
                memcpy(&payload_len, &packet_buf[8], 2);
                if (payload_len == 32) {
                  uint8_t session_key[16];
                  if (protocol_derive_session_key(packet_buf + 10, 32,
                                                  session_key)) {
                    protocol_activate_crypto(session_key);
                    uint8_t buf[32];
                    int len = encode_packet(buf, sizeof(buf),
                                            TYPE_SESSION_READY, NULL, 0,
                                            STREAM_CONTROL, 0, 0, 0, 0, 0, 0);
                    if (len > 0)
                      net_ws_send(buf, len);
                    ESP_LOGI(TAG, "Encryption session established");
                  }
                }
                rx_idx = 0;
                continue;
              }
              if (pkt.type == TYPE_SESSION_READY) {
                rx_idx = 0;
                continue;
              }

              // 非 ACK 包立即回复 ACK
              if (pkt.type != TYPE_ACK) {
                uint8_t ack_buf[32];
                int ack_len =
                    encode_ack(ack_buf, sizeof(ack_buf), pkt.type, pkt.epoch);
                if (ack_len > 0) {
                  net_ws_send(ack_buf, ack_len);
                  ESP_LOGI("NET", "Sent ACK for type=%d, epoch=%d", pkt.type,
                           pkt.epoch);
                }
              }

              // 统一交给聊天服务处理
              chat_service_handle_packet(&pkt);
            } else {
              ESP_LOGE(TAG, "CRC Error or invalid packet");
              ESP_LOG_BUFFER_HEX("RX_ERR", packet_buf, total_len);
            }
            rx_idx = 0;
          } else if (rx_idx > total_len || rx_idx >= sizeof(packet_buf)) {
            rx_idx = 0;
          }
        }
      }
      vRingbufferReturnItem(g_rx_ringbuf, (void *)data);
    }
  }
}

/* ---------- 重连任务 ---------- */
static void reconnect_task(void *pvParameters) {
  const net_config_t *cfg = (const net_config_t *)pvParameters;
  const int retry_interval_ms = 5000;
  ESP_LOGI(TAG, "reconnect_task started");
  while (1) {
    bool ready;
    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    ready = g_ws_ready;
    xSemaphoreGive(g_ws_mutex);
    if (!ready) {
      notify_status(NET_STATUS_RECONNECTING, "正在重新连接...");
      if (g_ws_client) {
        esp_websocket_client_destroy(g_ws_client);
        g_ws_client = NULL;
      }
      char full_uri[256];
      snprintf(full_uri, sizeof(full_uri), "ws://%s:%d/chat/%s/%s/child",
               cfg->host, cfg->port, cfg->user_id, cfg->device_id);
      esp_websocket_client_config_t ws_cfg = {
          .uri = full_uri,
          .reconnect_timeout_ms = 10000,
          .network_timeout_ms = 10000,
      };
      g_ws_client = esp_websocket_client_init(&ws_cfg);
      if (g_ws_client) {
        esp_websocket_register_events(g_ws_client, WEBSOCKET_EVENT_ANY,
                                      ws_event_handler, NULL);
        esp_websocket_client_start(g_ws_client);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(retry_interval_ms));
  }
}

/* ---------- 对外接口 ---------- */
void net_adapter_init(const net_config_t *cfg) {
  if (cfg) {
    g_active_cfg = *cfg;
  }

  g_ws_mutex = xSemaphoreCreateMutex();
  g_tx_queue = xQueueCreate(PENDING_QUEUE_SIZE, sizeof(tx_msg_t));
  g_rx_ringbuf = xRingbufferCreate(RX_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);

  // 创建发送任务
  if (g_tx_task_handle == NULL) {
    s_tx_task_stack = (StackType_t *)heap_caps_malloc(
        NET_TX_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_tx_task_stack) {
      size_t depth = NET_TX_STACK_SIZE / sizeof(StackType_t);
      g_tx_task_handle = xTaskCreateStatic(net_tx_task, "net_tx", depth, NULL,
                                           6, s_tx_task_stack, &s_tx_task_tcb);
      if (g_tx_task_handle) {
        ESP_LOGI(TAG, "net_tx task created with PSRAM stack, size %d",
                 NET_TX_STACK_SIZE);
      } else {
        heap_caps_free(s_tx_task_stack);
        s_tx_task_stack = NULL;
        xTaskCreate(net_tx_task, "net_tx", NET_TX_STACK_SIZE, NULL, 6,
                    &g_tx_task_handle);
      }
    } else {
      xTaskCreate(net_tx_task, "net_tx", NET_TX_STACK_SIZE, NULL, 6,
                  &g_tx_task_handle);
    }
  }

  // 创建接收任务
  if (g_rx_task_handle == NULL) {
    s_rx_task_stack = (StackType_t *)heap_caps_malloc(
        NET_RX_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_rx_task_stack) {
      size_t depth = NET_RX_STACK_SIZE / sizeof(StackType_t);
      g_rx_task_handle = xTaskCreateStatic(net_rx_task, "net_rx", depth, NULL,
                                           6, s_rx_task_stack, &s_rx_task_tcb);
      if (g_rx_task_handle) {
        ESP_LOGI(TAG, "net_rx task created with PSRAM stack, size %d",
                 NET_RX_STACK_SIZE);
      } else {
        heap_caps_free(s_rx_task_stack);
        s_rx_task_stack = NULL;
        xTaskCreate(net_rx_task, "net_rx", NET_RX_STACK_SIZE, NULL, 6,
                    &g_rx_task_handle);
      }
    } else {
      xTaskCreate(net_rx_task, "net_rx", NET_RX_STACK_SIZE, NULL, 6,
                  &g_rx_task_handle);
    }
  }

  // 创建重连任务
  if (g_reconnect_task_handle == NULL) {
    s_reconn_task_stack = (StackType_t *)heap_caps_malloc(
        NET_RECONN_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_reconn_task_stack) {
      size_t depth = NET_RECONN_STACK_SIZE / sizeof(StackType_t);
      g_reconnect_task_handle = xTaskCreateStatic(
          reconnect_task, "net_reconn", depth, (void *)&g_active_cfg, 5,
          s_reconn_task_stack, &s_reconn_task_tcb);
      if (g_reconnect_task_handle) {
        ESP_LOGI(TAG, "net_reconn task created with PSRAM stack, size %d",
                 NET_RECONN_STACK_SIZE);
      } else {
        heap_caps_free(s_reconn_task_stack);
        s_reconn_task_stack = NULL;
        xTaskCreate(reconnect_task, "net_reconn", NET_RECONN_STACK_SIZE,
                    (void *)&g_active_cfg, 5, &g_reconnect_task_handle);
      }
    } else {
      xTaskCreate(reconnect_task, "net_reconn", NET_RECONN_STACK_SIZE,
                  (void *)&g_active_cfg, 5, &g_reconnect_task_handle);
    }
  }
}

void net_ws_send(const uint8_t *data, size_t len) {
  if (!g_tx_queue)
    return;
  if (len > PENDING_BUF_SIZE) {
    ESP_LOGW(TAG, "Packet too large (%d bytes)", len);
    return;
  }
  tx_msg_t msg;
  msg.data = malloc(len);
  if (!msg.data)
    return;
  memcpy(msg.data, data, len);
  msg.len = len;
  if (xQueueSend(g_tx_queue, &msg, pdMS_TO_TICKS(10)) != pdPASS) {
    ESP_LOGW(TAG, "TX Queue full, drop");
    free(msg.data);
  }
}

bool net_is_connected(void) {
  bool ready;
  xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
  ready = g_ws_ready && g_ws_client &&
          esp_websocket_client_is_connected(g_ws_client);
  xSemaphoreGive(g_ws_mutex);
  return ready;
}