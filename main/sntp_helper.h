#ifndef sntp_helper_H
#define sntp_helper_H

#include "esp_err.h"
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sntp_helper_init(void);

esp_err_t sntp_helper_time(const char *server, int timeout_ms);

esp_err_t sntp_helper_set_timezone(const char *tz_string);

time_t sntp_helper_get_last_timestamp(void);

uint64_t sntp_helper_get_timestamp_ms(void);

void sntp_helper_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* sntp_helper_H */