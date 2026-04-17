#ifndef __PAGE_OTA_HEADER__
#define __PAGE_OTA_HEADER__

#include "stdint.h"

void page_ota_notify_status(const char *status_msg, int state);
void page_ota_notify_progress(uint32_t current_bytes, uint32_t total_bytes);
#endif