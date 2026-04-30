#include "esp_vfs_helper.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "vfs_helper";

#define VFS_COPY_BUF_SIZE 4096
#define VFS_MAX_READ_SIZE (128 * 1024)

typedef enum {
  VFS_MSG_READ_FILE,
  VFS_MSG_WRITE_FILE,
  VFS_MSG_DELETE_FILE,
  VFS_MSG_COPY_FILE,
  VFS_MSG_GET_SIZE,
  VFS_MSG_EXIT
} vfs_msg_type_t;

typedef struct {
  vfs_msg_type_t type;
  const char *path;
  const char *src_path;
  const char *dst_path;
  uint8_t *buf;
  size_t len;
  size_t *out_len;
  esp_err_t result;
  SemaphoreHandle_t sync;
} vfs_msg_t;

static QueueHandle_t s_vfs_queue = NULL;
static bool s_initialized = false;
static bool s_fs_mounted = false;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

#define DONE_MSG(msg, ret)                                                     \
  do {                                                                         \
    (msg)->result = (ret);                                                     \
    if ((msg)->sync)                                                           \
      xSemaphoreGive((msg)->sync);                                             \
    return;                                                                    \
  } while (0)

static void do_read_file(vfs_msg_t *msg) {
  struct stat st;
  if (stat(msg->path, &st) != 0)
    DONE_MSG(msg, ESP_ERR_NOT_FOUND);
  if (st.st_size > VFS_MAX_READ_SIZE)
    DONE_MSG(msg, ESP_ERR_INVALID_SIZE);

  FILE *f = fopen(msg->path, "rb");
  if (!f)
    DONE_MSG(msg, ESP_FAIL);

  uint8_t *buf =
      heap_caps_malloc(st.st_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) {
    fclose(f);
    DONE_MSG(msg, ESP_ERR_NO_MEM);
  }

  size_t read_bytes = fread(buf, 1, st.st_size, f);
  fclose(f);

  if (read_bytes != (size_t)st.st_size) {
    heap_caps_free(buf);
    DONE_MSG(msg, ESP_FAIL);
  }

  msg->buf = buf;
  if (msg->out_len)
    *msg->out_len = read_bytes;
  DONE_MSG(msg, ESP_OK);
}

static void do_copy_file(vfs_msg_t *msg) {
  FILE *src = fopen(msg->src_path, "rb");
  if (!src)
    DONE_MSG(msg, ESP_ERR_NOT_FOUND);

  FILE *dst = fopen(msg->dst_path, "wb");
  if (!dst) {
    fclose(src);
    DONE_MSG(msg, ESP_FAIL);
  }

  uint8_t *tmp = heap_caps_malloc(VFS_COPY_BUF_SIZE, MALLOC_CAP_INTERNAL);
  if (!tmp) {
    fclose(src);
    fclose(dst);
    DONE_MSG(msg, ESP_ERR_NO_MEM);
  }

  size_t n;
  esp_err_t res = ESP_OK;
  while ((n = fread(tmp, 1, VFS_COPY_BUF_SIZE, src)) > 0) {
    if (fwrite(tmp, 1, n, dst) != n) {
      res = ESP_FAIL;
      break;
    }
  }

  heap_caps_free(tmp);
  fclose(src);
  fclose(dst);
  DONE_MSG(msg, res);
}

static void vfs_task(void *arg) {
  vfs_msg_t *msg;
  ESP_LOGI(TAG, "VFS task started"); // 修改点：使用 TAG 消除未使用警告
  while (1) {
    if (xQueueReceive(s_vfs_queue, &msg, portMAX_DELAY) == pdTRUE) {
      switch (msg->type) {
      case VFS_MSG_READ_FILE:
        do_read_file(msg);
        break;
      case VFS_MSG_WRITE_FILE: {
        FILE *f = fopen(msg->path, "wb");
        size_t w = f ? fwrite(msg->buf, 1, msg->len, f) : 0;
        if (f)
          fclose(f);
        DONE_MSG(msg, (w == msg->len) ? ESP_OK : ESP_FAIL);
      } break;
      case VFS_MSG_COPY_FILE:
        do_copy_file(msg);
        break;
      case VFS_MSG_DELETE_FILE:
        DONE_MSG(msg, (unlink(msg->path) == 0) ? ESP_OK : ESP_FAIL);
        break;
      case VFS_MSG_GET_SIZE: {
        struct stat st;
        int r = stat(msg->path, &st);
        if (msg->out_len)
          *msg->out_len = st.st_size;
        DONE_MSG(msg, (r == 0) ? ESP_OK : ESP_ERR_NOT_FOUND);
      } break;
      case VFS_MSG_EXIT:
        DONE_MSG(msg, ESP_OK);
        vTaskDelete(NULL);
        return;
      default:
        DONE_MSG(msg, ESP_ERR_NOT_SUPPORTED);
        break;
      }
    }
  }
}

static esp_err_t send_sync_msg(vfs_msg_t *msg) {
  portENTER_CRITICAL(&s_lock);
  if (!s_initialized) {
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_INVALID_STATE;
  }
  portEXIT_CRITICAL(&s_lock);

  msg->sync = xSemaphoreCreateBinary();
  if (!msg->sync)
    return ESP_ERR_NO_MEM;

  if (xQueueSend(s_vfs_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
    vSemaphoreDelete(msg->sync);
    return ESP_ERR_TIMEOUT;
  }

  if (xSemaphoreTake(msg->sync, pdMS_TO_TICKS(10000)) != pdTRUE) {
    vSemaphoreDelete(msg->sync);
    return ESP_ERR_TIMEOUT;
  }

  vSemaphoreDelete(msg->sync);
  return msg->result;
}

esp_err_t vfs_helper_init(const char *base_path) {
  portENTER_CRITICAL(&s_lock);
  if (s_initialized) {
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
  }

  if (!s_fs_mounted) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = base_path,
        .partition_label = "storage",
        .format_if_mount_failed = true,
    };
    if (esp_vfs_littlefs_register(&conf) != ESP_OK) {
      portEXIT_CRITICAL(&s_lock);
      ESP_LOGE(TAG, "Failed to mount LittleFS");
      return ESP_FAIL;
    }
    s_fs_mounted = true;
  }

  s_vfs_queue = xQueueCreate(10, sizeof(vfs_msg_t *));
  if (!s_vfs_queue ||
      xTaskCreate(vfs_task, "vfs_task", 4096, NULL, 5, NULL) != pdPASS) {
    if (s_vfs_queue)
      vQueueDelete(s_vfs_queue);
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_NO_MEM;
  }

  s_initialized = true;
  portEXIT_CRITICAL(&s_lock);
  return ESP_OK;
}

esp_err_t vfs_helper_deinit(void) {
  vfs_msg_t msg = {.type = VFS_MSG_EXIT};
  esp_err_t ret = send_sync_msg(&msg);
  if (ret == ESP_OK) {
    portENTER_CRITICAL(&s_lock);
    vQueueDelete(s_vfs_queue);
    s_vfs_queue = NULL;
    s_initialized = false;
    portEXIT_CRITICAL(&s_lock);
  }
  return ret;
}

esp_err_t vfs_helper_read_file_to_ram(const char *path, uint8_t **out_buf,
                                      size_t *out_len) {
  if (!path || !out_buf || !out_len)
    return ESP_ERR_INVALID_ARG;
  vfs_msg_t msg = {.type = VFS_MSG_READ_FILE, .path = path, .out_len = out_len};
  esp_err_t err = send_sync_msg(&msg);
  if (err == ESP_OK)
    *out_buf = msg.buf;
  return err;
}

esp_err_t vfs_helper_write_file_from_ram(const char *path, const uint8_t *buf,
                                         size_t len) {
  if (!path || !buf || len == 0)
    return ESP_ERR_INVALID_ARG;
  vfs_msg_t msg = {.type = VFS_MSG_WRITE_FILE,
                   .path = path,
                   .buf = (uint8_t *)buf,
                   .len = len};
  return send_sync_msg(&msg);
}

esp_err_t vfs_helper_delete_file(const char *path) {
  if (!path)
    return ESP_ERR_INVALID_ARG;
  vfs_msg_t msg = {.type = VFS_MSG_DELETE_FILE, .path = path};
  return send_sync_msg(&msg);
}

esp_err_t vfs_helper_copy_file(const char *src, const char *dst) {
  if (!src || !dst)
    return ESP_ERR_INVALID_ARG;
  vfs_msg_t msg = {.type = VFS_MSG_COPY_FILE, .src_path = src, .dst_path = dst};
  return send_sync_msg(&msg);
}

esp_err_t vfs_helper_get_file_size(const char *path, size_t *size) {
  if (!path || !size)
    return ESP_ERR_INVALID_ARG;
  vfs_msg_t msg = {.type = VFS_MSG_GET_SIZE, .path = path, .out_len = size};
  return send_sync_msg(&msg);
}