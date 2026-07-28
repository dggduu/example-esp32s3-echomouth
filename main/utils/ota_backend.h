#ifndef OTA_BACKEND_H
#define OTA_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  OTA_STATE_IDLE,
  OTA_STATE_CONNECTED,
  OTA_STATE_TRANSFERRING,
  OTA_STATE_SUCCESS,
  OTA_STATE_FAILED
} ota_state_t;

bool ota_backend_init(const char *device_name);
void ota_backend_deinit(void);
#endif