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
  TaskHandle_t caller;
} vfs_msg_t;

static QueueHandle_t s_vfs_queue = NULL;
static bool s_initialized = false;
static bool s_fs_mounted = false;
static SemaphoreHandle_t s_init_mutex = NULL;

#define DONE_MSG(msg, ret)                                                     \
  do {                                                                         \
    (msg)->result = (ret);                                                     \
    if ((msg)->caller) {                                                       \
      xTaskNotifyGive((msg)->caller);                                          \
    }                                                                          \
  } while (0)

/* 内部函数                                                */
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

  char tmp_path[CONFIG_LITTLEFS_OBJ_NAME_LEN + 10];
  int ret = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", msg->dst_path);
  if (ret < 0 || ret >= sizeof(tmp_path)) {
    fclose(src);
    DONE_MSG(msg, ESP_ERR_INVALID_ARG);
  }

  FILE *dst = fopen(tmp_path, "wb");
  if (!dst) {
    fclose(src);
    DONE_MSG(msg, ESP_FAIL);
  }

  uint8_t *tmp = heap_caps_malloc(VFS_COPY_BUF_SIZE, MALLOC_CAP_INTERNAL);
  if (!tmp) {
    fclose(src);
    fclose(dst);
    unlink(tmp_path);
    DONE_MSG(msg, ESP_ERR_NO_MEM);
  }

  size_t n;
  bool write_ok = true;
  while ((n = fread(tmp, 1, VFS_COPY_BUF_SIZE, src)) > 0) {
    if (fwrite(tmp, 1, n, dst) != n) {
      write_ok = false;
      break;
    }
  }

  heap_caps_free(tmp);
  fclose(src);
  fclose(dst);

  if (!write_ok) {
    unlink(tmp_path);
    DONE_MSG(msg, ESP_FAIL);
  }

  if (rename(tmp_path, msg->dst_path) != 0) {
    unlink(tmp_path);
    DONE_MSG(msg, ESP_FAIL);
  }

  DONE_MSG(msg, ESP_OK);
}

/* VFS 任务主循环                                          */
static void vfs_task(void *arg) {
  vfs_msg_t *msg;
  bool running = true;

  ESP_LOGI(TAG, "VFS task started (stack min free: %u)",
           uxTaskGetStackHighWaterMark(NULL));

  while (running) {
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
        if (r == 0 && msg->out_len)
          *msg->out_len = st.st_size;
        DONE_MSG(msg, (r == 0) ? ESP_OK : ESP_ERR_NOT_FOUND);
      } break;

      case VFS_MSG_EXIT:
        msg->result = ESP_OK;
        if (msg->caller) {
          xTaskNotifyGive(msg->caller);
        }
        running = false;
        break;

      default:
        DONE_MSG(msg, ESP_ERR_NOT_SUPPORTED);
        break;
      }
    }
  }

  ESP_LOGI(TAG, "VFS task exiting");
  vTaskDelete(NULL);

  while (1) {
    vTaskDelay(portMAX_DELAY); // 保险，永远不允许 return
  }
}

/* 同步消息发送                                            */
static esp_err_t send_sync_msg(vfs_msg_t *msg) {
  if (!s_initialized)
    return ESP_ERR_INVALID_STATE;

  msg->caller = xTaskGetCurrentTaskHandle();

  if (xQueueSend(s_vfs_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
    msg->caller = NULL;
    return ESP_ERR_TIMEOUT;
  }

  if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000)) == 0) {
    return ESP_ERR_TIMEOUT;
  }

  return msg->result;
}

/* 初始化                                                   */
void vfs_helper_early_init(void) {
  if (!s_init_mutex) {
    s_init_mutex = xSemaphoreCreateMutex();
  }
}

esp_err_t vfs_helper_init(const char *base_path) {
  if (!s_init_mutex)
    return ESP_ERR_INVALID_STATE;

  xSemaphoreTake(s_init_mutex, portMAX_DELAY);

  if (s_initialized) {
    xSemaphoreGive(s_init_mutex);
    return ESP_OK;
  }

  if (!s_fs_mounted) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = base_path,
        .partition_label = "storage",
        .format_if_mount_failed = true,
    };
    if (esp_vfs_littlefs_register(&conf) != ESP_OK) {
      xSemaphoreGive(s_init_mutex);
      return ESP_FAIL;
    }
    s_fs_mounted = true;
  }

  s_vfs_queue = xQueueCreate(10, sizeof(vfs_msg_t *));
  if (!s_vfs_queue) {
    xSemaphoreGive(s_init_mutex);
    return ESP_ERR_NO_MEM;
  }

  if (xTaskCreate(vfs_task, "vfs_task", 8192, NULL, 5, NULL) != pdPASS) {
    vQueueDelete(s_vfs_queue);
    s_vfs_queue = NULL;
    xSemaphoreGive(s_init_mutex);
    return ESP_ERR_NO_MEM;
  }

  s_initialized = true;
  xSemaphoreGive(s_init_mutex);
  return ESP_OK;
}

esp_err_t vfs_helper_deinit(void) {
  vfs_msg_t msg = {.type = VFS_MSG_EXIT};
  esp_err_t ret = send_sync_msg(&msg);

  if (ret == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(50));
    xSemaphoreTake(s_init_mutex, portMAX_DELAY);
    vQueueDelete(s_vfs_queue);
    s_vfs_queue = NULL;
    s_initialized = false;
    xSemaphoreGive(s_init_mutex);
  }

  return ret;
}

/* 公开 API                                                 */
esp_err_t vfs_helper_read_file_to_ram(const char *path, uint8_t **out_buf,
                                      size_t *out_len) {
  if (!path || !out_buf || !out_len)
    return ESP_ERR_INVALID_ARG;

  vfs_msg_t msg = {
      .type = VFS_MSG_READ_FILE,
      .path = path,
      .out_len = out_len,
  };

  esp_err_t err = send_sync_msg(&msg);
  if (err == ESP_OK)
    *out_buf = msg.buf;

  return err;
}

esp_err_t vfs_helper_write_file_from_ram(const char *path, const uint8_t *buf,
                                         size_t len) {
  if (!path || !buf || len == 0)
    return ESP_ERR_INVALID_ARG;

  vfs_msg_t msg = {
      .type = VFS_MSG_WRITE_FILE,
      .path = path,
      .buf = (uint8_t *)buf,
      .len = len,
  };

  return send_sync_msg(&msg);
}

esp_err_t vfs_helper_delete_file(const char *path) {
  if (!path)
    return ESP_ERR_INVALID_ARG;

  vfs_msg_t msg = {
      .type = VFS_MSG_DELETE_FILE,
      .path = path,
  };

  return send_sync_msg(&msg);
}

esp_err_t vfs_helper_copy_file(const char *src, const char *dst) {
  if (!src || !dst)
    return ESP_ERR_INVALID_ARG;

  vfs_msg_t msg = {
      .type = VFS_MSG_COPY_FILE,
      .src_path = src,
      .dst_path = dst,
  };

  return send_sync_msg(&msg);
}

esp_err_t vfs_helper_get_file_size(const char *path, size_t *size) {
  if (!path || !size)
    return ESP_ERR_INVALID_ARG;

  vfs_msg_t msg = {
      .type = VFS_MSG_GET_SIZE,
      .path = path,
      .out_len = size,
  };

  return send_sync_msg(&msg);
}
