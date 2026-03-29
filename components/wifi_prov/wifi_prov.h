#ifndef _WIFI_PROV_H__
#define _WIFI_PROV_H__

#include "esp_err.h"

esp_err_t wifi_prov_init();

// 工具函数：用于临时测试prov 时直接开启NVS
void wifi_prov_nvs_init();

#endif
