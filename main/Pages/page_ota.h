#ifndef __PAGE_OTA_HEADER__
#define __PAGE_OTA_HEADER__

#include "stdint.h"

// 保留出的接口，用于给OTA_BackEnd调用
void page_ota_notify_status(const char *status_msg, int state);
void page_ota_notify_progress(uint32_t current_bytes, uint32_t total_bytes);
#endif // !__PAGE_OTA_HEADER__