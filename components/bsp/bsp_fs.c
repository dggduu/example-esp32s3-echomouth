#include "bsp_fs.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "lvgl.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "BSP_FS";

#define BSP_FS_BASE_PATH "/littlefs"
#define BSP_FS_PARTITION "storage"
#define BSP_FS_LETTER 'L'

/* ── LVGL v9 FS driver callbacks (standard C file I/O over VFS) ── */

static void *lv_fs_open_cb(lv_fs_drv_t *drv, const char *path,
                           lv_fs_mode_t mode) {
  const char *flags = (mode == LV_FS_MODE_WR)   ? "wb"
                      : (mode == LV_FS_MODE_RD) ? "rb"
                                                : "r+b";
  char full[256];
  snprintf(full, sizeof(full), "%s/%s", BSP_FS_BASE_PATH, path);
  return fopen(full, flags);
}

static lv_fs_res_t lv_fs_close_cb(lv_fs_drv_t *drv, void *file_p) {
  return fclose((FILE *)file_p) == 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t lv_fs_read_cb(lv_fs_drv_t *drv, void *file_p, void *buf,
                                 uint32_t btr, uint32_t *br) {
  *br = (uint32_t)fread(buf, 1, btr, (FILE *)file_p);
  return LV_FS_RES_OK;
}

static lv_fs_res_t lv_fs_write_cb(lv_fs_drv_t *drv, void *file_p,
                                  const void *buf, uint32_t btw, uint32_t *bw) {
  *bw = (uint32_t)fwrite(buf, 1, btw, (FILE *)file_p);
  return LV_FS_RES_OK;
}

static lv_fs_res_t lv_fs_seek_cb(lv_fs_drv_t *drv, void *file_p, uint32_t pos,
                                 lv_fs_whence_t whence) {
  int w = (whence == LV_FS_SEEK_SET)   ? SEEK_SET
          : (whence == LV_FS_SEEK_CUR) ? SEEK_CUR
                                       : SEEK_END;
  fseek((FILE *)file_p, pos, w);
  return LV_FS_RES_OK;
}

static lv_fs_res_t lv_fs_tell_cb(lv_fs_drv_t *drv, void *file_p,
                                 uint32_t *pos_p) {
  long p = ftell((FILE *)file_p);
  *pos_p = (uint32_t)(p >= 0 ? p : 0);
  return LV_FS_RES_OK;
}

/* ── LVGL v9 directory callbacks (return void* handle) ── */

typedef struct {
  DIR *dir;
  char base[256];
} lv_dir_ctx_t;

static void *lv_fs_dir_open_cb(lv_fs_drv_t *drv, const char *path) {
  char full[256];
  snprintf(full, sizeof(full), "%s/%s", BSP_FS_BASE_PATH, path);
  size_t len = strlen(full);
  if (len > 0 && full[len - 1] == '/')
    full[len - 1] = '\0';

  DIR *d = opendir(full);
  if (!d)
    return NULL;

  lv_dir_ctx_t *ctx = lv_malloc(sizeof(lv_dir_ctx_t));
  if (!ctx) {
    closedir(d);
    return NULL;
  }
  ctx->dir = d;
  strncpy(ctx->base, full, sizeof(ctx->base) - 1);
  return ctx;
}

static lv_fs_res_t lv_fs_dir_read_cb(lv_fs_drv_t *drv, void *rddir_p, char *fn,
                                     uint32_t fn_len) {
  if (!rddir_p)
    return LV_FS_RES_UNKNOWN;
  lv_dir_ctx_t *ctx = (lv_dir_ctx_t *)rddir_p;
  struct dirent *entry;
  while ((entry = readdir(ctx->dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    strncpy(fn, entry->d_name, fn_len - 1);
    fn[fn_len - 1] = '\0';
    return LV_FS_RES_OK;
  }
  return LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t lv_fs_dir_close_cb(lv_fs_drv_t *drv, void *rddir_p) {
  if (!rddir_p)
    return LV_FS_RES_OK;
  lv_dir_ctx_t *ctx = (lv_dir_ctx_t *)rddir_p;
  closedir(ctx->dir);
  lv_free(ctx);
  return LV_FS_RES_OK;
}

/* ═══════════════════════════════════════════
   Public
   ═══════════════════════════════════════════ */

esp_err_t bsp_fs_init(void) {
  esp_vfs_littlefs_conf_t conf = {
      .base_path = BSP_FS_BASE_PATH,
      .partition_label = BSP_FS_PARTITION,
      .format_if_mount_failed = false,
  };
  esp_err_t ret = esp_vfs_littlefs_register(&conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
    return ret;
  }
  size_t total = 0, used = 0;
  esp_littlefs_info(conf.partition_label, &total, &used);
  ESP_LOGI(TAG, "LittleFS mounted (%d/%d bytes)", used, total);

  lv_fs_drv_t fs_drv;
  lv_fs_drv_init(&fs_drv);
  fs_drv.letter = BSP_FS_LETTER;
  fs_drv.cache_size = 512;
  fs_drv.open_cb = lv_fs_open_cb;
  fs_drv.close_cb = lv_fs_close_cb;
  fs_drv.read_cb = lv_fs_read_cb;
  fs_drv.write_cb = lv_fs_write_cb;
  fs_drv.seek_cb = lv_fs_seek_cb;
  fs_drv.tell_cb = lv_fs_tell_cb;
  fs_drv.dir_open_cb = lv_fs_dir_open_cb;
  fs_drv.dir_read_cb = lv_fs_dir_read_cb;
  fs_drv.dir_close_cb = lv_fs_dir_close_cb;
  lv_fs_drv_register(&fs_drv);
  ESP_LOGI(TAG, "LVGL FS driver registered (drive %c:)", BSP_FS_LETTER);
  return ESP_OK;
}
