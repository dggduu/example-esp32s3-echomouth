#ifndef BSP_FS_H
#define BSP_FS_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mount LittleFS and register LVGL filesystem driver.
 *
 * Mounts the "storage" partition at "/littlefs".
 * Registers LVGL FS driver with drive letter 'L' for lv_file_explorer.
 */
esp_err_t bsp_fs_init(void);

#ifdef __cplusplus
}
#endif

#endif
