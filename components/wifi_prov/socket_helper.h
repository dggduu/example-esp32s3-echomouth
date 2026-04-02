#ifndef SOCKET_PROV_H
#define SOCKET_PROV_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 阻塞式连接服务器
 * @param host 支持 "192.168.1.1" 或 "ubuntu-s3.local"
 * @param port 端口号
 * @return 成功返回 socket fd，失败返回 -1
 */
int socket_prov_connect_ws(const char *uri);

/**
 * @brief 发送原始字节流
 */
ssize_t socket_prov_send(int fd, const void *data, size_t len, int flags);

/**
 * @brief 接收原始字节流
 */
ssize_t socket_prov_recv(int fd, void *buffer, size_t len, int flags);

/**
 * @brief 关闭连接
 */
void socket_prov_close(int fd);

#ifdef __cplusplus
}
#endif

#endif