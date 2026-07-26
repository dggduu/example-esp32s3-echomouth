#ifndef IMG_QUEUE_H
#define IMG_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  IMG_TYPE_MONITOR = 0,
  IMG_TYPE_MANUAL = 1,
} img_job_type_t;

typedef enum {
  IMG_PRIORITY_LOW = 0,
  IMG_PRIORITY_HIGH = 1,
} img_priority_t;

typedef void (*img_upload_callback_t)(bool success, const char *image_key,
                                      void *user_data);
typedef struct {
  char path[128];
  int task_id;
  img_priority_t priority;
  uint8_t retry_count;
  img_job_type_t type;

  img_upload_callback_t on_complete;
  void *user_data;

  char image_key[128];
} img_job_t;

void img_queue_init(void);

bool img_queue_push(const img_job_t *job);

bool img_queue_peek(img_job_t *out_job);

bool img_queue_commit(void);

void img_queue_update_retry(uint8_t new_retry_count);

int img_queue_get_count(void);

bool img_queue_is_full(void);

// 网络状态回调：用于离线/恢复时暂停/冲传上传队列
void img_queue_set_network_up(bool up);
bool img_queue_is_network_up(void);

#ifdef __cplusplus
}
#endif

#endif