// net_adapter.h
#ifndef NET_ADAPTER_H
#define NET_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  char host[64];
  int port;
  char user_id[16];
  char device_id[16];
} net_config_t;

typedef enum {
  NET_STATUS_CONNECTED,
  NET_STATUS_DISCONNECTED,
  NET_STATUS_RECONNECTING
} net_status_t;

typedef void (*net_status_callback_t)(net_status_t status, const char *msg);

void net_set_status_callback(net_status_callback_t cb);
void net_reset_status_callback(void); // 恢复默认 Toast 回调
bool net_is_connected(void);
void net_ws_connect(const net_config_t *cfg);
void net_ws_send(const uint8_t *data, size_t len);
bool net_ws_fetch_rx(uint8_t *out_buf, size_t *out_len);
void net_ws_disconnect(void);
void net_start_reconnect_task(const net_config_t *cfg);

void net_start_global_receiver_task(void);

#endif