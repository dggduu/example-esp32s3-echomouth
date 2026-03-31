#include "net_adapter.h"
#include "esp_log.h"
#include "socket_prov.h"
#include <errno.h>
#include <string.h>

#include <sys/socket.h>

static const char *TAG = "NET_ADAPTER";

// 全局维护的唯一 Socket 描述符
static int g_socket_fd = -1;
static net_config_t g_active_cfg;

bool net_is_connected(void) { return g_socket_fd >= 0; }

void net_ws_connect(const net_config_t *cfg) {
  if (net_is_connected()) {
    net_ws_disconnect();
  }

  if (cfg) {
    g_active_cfg = *cfg;
  }

  char full_uri[128];
  snprintf(full_uri, sizeof(full_uri), "ws://%s:%d/chat/%s/%s/child", cfg->host,
           cfg->port, cfg->user_id, cfg->device_id);

  ESP_LOGI(TAG, "Connecting to %s", full_uri);

  g_socket_fd = socket_prov_connect_ws(full_uri);

  if (g_socket_fd < 0) {
    ESP_LOGE(TAG, "BSP connect failed");
  } else {
    ESP_LOGI(TAG, "Net Adapter ready, FD=%d", g_socket_fd);
  }
}

void net_ws_send(const uint8_t *data, size_t len) {
  if (!net_is_connected())
    return;

  // 调用 BSP 层发送
  ssize_t ret = socket_prov_send(g_socket_fd, data, len, 0);

  if (ret < 0) {
    ESP_LOGE(TAG, "Send failed, triggering disconnect");
    net_ws_disconnect();
  }
}

bool net_ws_fetch_rx(uint8_t *out_buf, size_t *out_len) {
  if (!net_is_connected())
    return false;

  ssize_t ret = socket_prov_recv(g_socket_fd, out_buf, 1024, MSG_DONTWAIT);

  if (ret > 0) {
    *out_len = (size_t)ret;
    return true;
  } else if (ret == 0) {
    ESP_LOGW(TAG, "Remote closed connection");
    net_ws_disconnect();
  } else {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      ESP_LOGE(TAG, "Recv error %d", errno);
      net_ws_disconnect();
    }
  }

  return false;
}

void net_ws_disconnect(void) {
  if (g_socket_fd >= 0) {
    socket_prov_close(g_socket_fd);
    g_socket_fd = -1;
    ESP_LOGI(TAG, "Disconnected");
  }
}