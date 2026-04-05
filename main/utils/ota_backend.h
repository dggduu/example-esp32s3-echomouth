#ifndef OTA_BACKEND_H
#define OTA_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

// 定义 OTA 状态机枚举
typedef enum {
  OTA_STATE_IDLE,
  OTA_STATE_CONNECTED,
  OTA_STATE_TRANSFERRING,
  OTA_STATE_SUCCESS,
  OTA_STATE_FAILED
} ota_state_t;

// 初始化 OTA 后台服务
bool ota_backend_init(void);
void ota_backend_deinit(void);
#endif // OTA_BACKEND_H