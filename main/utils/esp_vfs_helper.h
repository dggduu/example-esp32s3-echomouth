#ifndef ESP_VFS_HELPER_H
#define ESP_VFS_HELPER_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t vfs_helper_init(const char *base_path);
esp_err_t vfs_helper_deinit(void);
esp_err_t vfs_helper_read_file_to_ram(const char *path, uint8_t **out_buf,
                                      size_t *out_len);
esp_err_t vfs_helper_write_file_from_ram(const char *path, const uint8_t *buf,
                                         size_t len);
esp_err_t vfs_helper_delete_file(const char *path);
esp_err_t vfs_helper_copy_file(const char *src, const char *dst);
esp_err_t vfs_helper_get_file_size(const char *path, size_t *size);

#ifdef __cplusplus
}
#endif

#endif