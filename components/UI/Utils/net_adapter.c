// net_adapter.c
#include "net_adapter.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gs_portal.h"
#include "socket_helper.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>

#include "protocol.h"

#include "chat_service.h"

static const char *TAG = "NET_ADAPTER";

static int g_socket_fd = -1;
static net_config_t g_active_cfg;
static net_status_callback_t g_user_status_cb = NULL;
static TaskHandle_t g_reconnect_task_handle = NULL;

static TaskHandle_t g_receiver_task_handle = NULL;

// ---------- 默认回调（Toast） ----------
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

void net_reset_status_callback(void) {
  g_user_status_cb = NULL; // 恢复使用默认回调
}

// ---------- 连接管理 ----------
bool net_is_connected(void) { return g_socket_fd >= 0; }

void net_ws_connect(const net_config_t *cfg) {
  if (net_is_connected()) {
    net_ws_disconnect();
  }

  if (cfg) {
    g_active_cfg = *cfg;
  }

  char full_uri[128];
  snprintf(full_uri, sizeof(full_uri), "ws://%s:%d/chat/%s/%s/child",
           g_active_cfg.host, g_active_cfg.port, g_active_cfg.user_id,
           g_active_cfg.device_id);

  ESP_LOGI(TAG, "Connecting to %s", full_uri);
  g_socket_fd = socket_prov_connect_ws(full_uri);

  if (g_socket_fd < 0) {
    ESP_LOGE(TAG, "BSP connect failed");
    notify_status(NET_STATUS_DISCONNECTED, "连接失败");
  } else {
    ESP_LOGI(TAG, "Net Adapter ready, FD=%d", g_socket_fd);
    notify_status(NET_STATUS_CONNECTED, "已连接到服务器");
  }
}

void net_ws_send(const uint8_t *data, size_t len) {
  if (!net_is_connected())
    return;
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
    notify_status(NET_STATUS_DISCONNECTED, "连接已断开");
  }
}

// ---------- 后台重连任务 ----------
static void reconnect_task(void *pvParameters) {
  const net_config_t *cfg = (const net_config_t *)pvParameters;
  const int retry_interval_ms = 5000;

  while (1) {
    if (!net_is_connected()) {
      ESP_LOGI(TAG, "Attempting reconnect...");
      notify_status(NET_STATUS_RECONNECTING, "正在重新连接...");
      net_ws_connect(cfg);
    }
    vTaskDelay(pdMS_TO_TICKS(retry_interval_ms));
  }
}

void net_start_reconnect_task(const net_config_t *cfg) {
  if (g_reconnect_task_handle == NULL) {
    xTaskCreate(reconnect_task, "net_reconnect", 4096, (void *)cfg, 5,
                &g_reconnect_task_handle);
  }
}

static void global_receiver_task(void *pvParameters) {
  uint8_t buf[1024];
  size_t len;

  while (1) {
    if (net_ws_fetch_rx(buf, &len)) {
      protocol_packet_t pkt;
      if (decode_packet(buf, len, &pkt)) {
        device_mode_t mode = protocol_get_mode();

        if (mode == DEVICE_MODE_HOME) {
          // HOME 模式：只处理 TYPE_REASONING，用于 L2 提醒
          if (pkt.type == TYPE_REASONING) {
            // 显示 Toast 提示
            gs_toast_show(pkt.reasoning_content, GS_TOAST_INFO);
          }
          // 其他包忽略
        } else if (mode == DEVICE_MODE_CHAT_LIVE ||
                   mode == DEVICE_MODE_CHAT_HISTORY) {
          // 聊天模式：将包转交给聊天服务处理
          chat_service_handle_packet(&pkt);
        }
        // 其他模式（如 OTA 等）可忽略
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // 短暂延时，防止饥饿
  }
}

void net_start_global_receiver_task(void) {
  if (g_receiver_task_handle == NULL) {
    xTaskCreate(global_receiver_task, "net_recv", 4096, NULL, 5,
                &g_receiver_task_handle);
  }
}