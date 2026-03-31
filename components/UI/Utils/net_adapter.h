#ifndef NET_ADAPTER_H
#define NET_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  const char *host;    // 如 "ubuntu-s3.local"
  uint16_t port;       // 如 3000
  const char *user_id; // 用于后续协议层握手
  const char *device_id;
} net_config_t;

/**
 * @brief 阻塞式连接，但通过配置驱动
 */
void net_ws_connect(const net_config_t *cfg);

/**
 * @brief 发送原始字节流
 */
void net_ws_send(const uint8_t *data, size_t len);

/**
 * @brief 非阻塞式接收，适配 chat_service_loop 的 while 循环
 */
bool net_ws_fetch_rx(uint8_t *out_buf, size_t *out_len);

/**
 * @brief 检查物理链路是否连通
 */
bool net_is_connected(void);

/**
 * @brief 主动断开
 */
void net_ws_disconnect(void);

#endif