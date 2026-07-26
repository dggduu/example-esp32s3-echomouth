#ifndef HTTP_CLIENT_HELPER_H
#define HTTP_CLIENT_HELPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool http_helper_init(void);

bool http_get_json(const char *path, char *response, int max_len);

bool http_post_json(const char *path, const char *json);

bool http_post_json_with_response(const char *path, const char *json,
                                  char *response, int max_len);

bool http_put_binary(const char *url, uint8_t *data, int len);

/**
 * @brief PUT 二进制数据，可覆写 HTTP Host 头
 *
 * 用于 S3 预签名 URL 场景：URL 已改写为 IP 直连，但必须保留原始 Host
 * 以匹配 AWS V4 签名。
 *
 * @param url       完整 URL（可用 IP）
 * @param data      数据指针
 * @param len       数据长度
 * @param host_hdr  覆写的 Host 头（如 "aobara-pc.local:9000"），传 NULL 则不覆写
 */
bool http_put_binary_with_host(const char *url, uint8_t *data, int len,
                               const char *host_hdr);

bool get_mdns_server_ip(char *ip_buf, size_t buf_len);

bool http_ping_server(void);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_CLIENT_HELPER_H */