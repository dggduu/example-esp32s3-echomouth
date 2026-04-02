#include "stdint.h"
#include <stdbool.h>


bool http_get_json(const char *url, char *response, int max_len);
bool http_put_binary(const char *url, uint8_t *data, int len);
bool http_post_json(const char *url, const char *json);
