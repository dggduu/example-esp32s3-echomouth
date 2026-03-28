#include "net_adapter.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------- 队列配置 ----------------------

void net_ws_connect(const net_config_t *cfg) {}

void net_ws_send(const uint8_t *data, size_t len) {}

bool net_ws_fetch_rx(uint8_t *out_buf, size_t *out_len) { return true; }

bool net_is_connected(void) { return true; }

void net_ws_disconnect(void) {}