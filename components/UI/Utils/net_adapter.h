#ifndef NET_ADAPTER_H
#define NET_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  NET_STATUS_DISCONNECTED,
  NET_STATUS_CONNECTING,
  NET_STATUS_CONNECTED,
  NET_STATUS_RECONNECTING
} net_status_t;

typedef void (*net_status_callback_t)(net_status_t status, const char *msg);

typedef struct {
  char host[64];
  uint16_t port;
  char user_id[32];
  char device_id[32];
} net_config_t;

// 初始化网络适配器
void net_adapter_init(const net_config_t *cfg);

// 发送二进制数据
void net_ws_send(const uint8_t *data, size_t len);

// 连接状态查询
bool net_is_connected(void);

// 状态回调
void net_set_status_callback(net_status_callback_t cb);
void net_reset_status_callback(void);

#endif