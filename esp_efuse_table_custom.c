/*
 * SPDX-FileCopyrightText: 2017-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "esp_efuse.h"
#include <assert.h>
#include "esp_efuse_table.h"

// md5_digest_table af98df7c9a8175e76d61af53b8d426ff
// This file was generated from the file esp_efuse_table.csv. DO NOT CHANGE THIS FILE MANUALLY.
// If you want to change some fields, you need to change esp_efuse_table.csv file
// then run `efuse_common_table` or `efuse_custom_table` command it will generate this file.
// To show efuse_table run the command 'show_efuse_table'.

static const esp_efuse_desc_t USER_DATA_DEVICE_UUID[] = {
    {EFUSE_BLK3, 0, 128}, 	 // Device unique UUID (128 bits),
};





const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_DEVICE_UUID[] = {
    &USER_DATA_DEVICE_UUID[0],    		// Device unique UUID (128 bits)
    NULL
};
