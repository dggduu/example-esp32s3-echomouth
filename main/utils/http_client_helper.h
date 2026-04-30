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

bool get_mdns_server_ip(char *ip_buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_CLIENT_HELPER_H */