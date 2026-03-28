#ifndef NET_ADAPTER_H
#define NET_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// #ifndef MAX_MSG_LEN
// #define MAX_MSG_LEN 1024
// #endif

typedef struct {
  const char *host;
  int port;
  const char *user_id;
  const char *device_id;
} net_config_t;

void net_ws_connect(const net_config_t *cfg);
void net_ws_send(const uint8_t *data, size_t len);
bool net_ws_fetch_rx(uint8_t *out_buf, size_t *out_len);
bool net_is_connected(void);
void net_ws_disconnect(void);

#endif