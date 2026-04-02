#include "socket_helper.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/ringbuf.h"
#include <errno.h>

static const char *TAG = "SOCKET_PROV";
static esp_websocket_client_handle_t ws_client = NULL;
static RingbufHandle_t rx_ring_buf = NULL;

static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data) {
  esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
  // 只处理二进制数据帧
  if (event_id == WEBSOCKET_EVENT_DATA &&
      data->op_code == WS_TRANSPORT_OPCODES_BINARY) {
    if (rx_ring_buf && data->data_ptr && data->data_len > 0) {
      // 字节缓冲区允许直接推入任意长度数据
      xRingbufferSend(rx_ring_buf, data->data_ptr, data->data_len, 0);
    }
  }
}

int socket_prov_connect_ws(const char *uri) {
  if (ws_client)
    socket_prov_close(0);

  if (!rx_ring_buf) {
    rx_ring_buf = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);
  }

  const esp_websocket_client_config_t ws_cfg = {
      .uri = uri,
      .reconnect_timeout_ms = 10000,
      .network_timeout_ms = 10000,
  };

  ws_client = esp_websocket_client_init(&ws_cfg);
  esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY,
                                websocket_event_handler, NULL);

  if (esp_websocket_client_start(ws_client) != ESP_OK) {
    return -1;
  }

  return 1; // 返回虚拟 FD
}

ssize_t socket_prov_send(int fd, const void *data, size_t len, int flags) {
  if (!ws_client || !esp_websocket_client_is_connected(ws_client))
    return -1;
  return esp_websocket_client_send_bin(ws_client, (const char *)data, len,
                                       portMAX_DELAY);
}

ssize_t socket_prov_recv(int fd, void *buffer, size_t len, int flags) {
  if (!rx_ring_buf)
    return -1;

  size_t received_size = 0;
  uint8_t *data =
      (uint8_t *)xRingbufferReceiveUpTo(rx_ring_buf, &received_size, 0, len);

  if (data != NULL && received_size > 0) {
    memcpy(buffer, data, received_size);
    vRingbufferReturnItem(rx_ring_buf, (void *)data);
    return (ssize_t)received_size;
  }

  errno = EAGAIN;
  return -1;
}

void socket_prov_close(int fd) {
  if (ws_client) {
    esp_websocket_client_stop(ws_client);
    esp_websocket_client_destroy(ws_client);
    ws_client = NULL;
  }
  if (rx_ring_buf) {
    vRingbufferDelete(rx_ring_buf);
    rx_ring_buf = NULL;
  }
}