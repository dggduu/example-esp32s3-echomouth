#ifndef __S3_HELPER__
#define __S3_HELPER__

#include "stdint.h"

typedef enum { UPLOAD_TYPE_MONITOR = 0, UPLOAD_TYPE_MANUAL = 1 } upload_type_t;

typedef struct {
  upload_type_t type;
  int32_t device_id;
  int32_t task_id;

  void *callback_ctx;
} upload_user_ctx_t;

void uploader_task_start(void);

#endif
