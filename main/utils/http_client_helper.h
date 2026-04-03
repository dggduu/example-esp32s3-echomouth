#include <stdbool.h>
#include <stdint.h>

bool http_helper_init(void);

bool http_get_json(const char *path, char *response, int max_len);

bool http_post_json(const char *path, const char *json);

bool http_put_binary(const char *path, uint8_t *data, int len);
