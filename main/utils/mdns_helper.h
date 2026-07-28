#ifndef MDNS_HELPER_H
#define MDNS_HELPER_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief 同步初始化 mDNS 模块
 * @param local_hostname 本地主机名（如 "esp32-s3"）
 * @return true 初始化成功, false 初始化失败
 */
bool mdns_helper_init(const char *local_hostname);

bool mdns_helper_is_ready(void);

bool mdns_helper_resolve_ip(const char *target_hostname, char *ip_buf,
                            size_t buf_len);

#endif // MDNS_HELPER_H