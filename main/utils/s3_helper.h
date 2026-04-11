#ifndef __S3_HELPER__
#define __S3_HELPER__

#include "stdint.h"

// 上传任务类型
typedef enum {
  UPLOAD_TYPE_MONITOR = 0, // 定时监控，调用 /device/image
  UPLOAD_TYPE_MANUAL = 1   // 手动拍照，调用 /device/image/result
} upload_type_t;

// 扩展 img_job_t 中的 user_data，用于传递额外参数
typedef struct {
  upload_type_t type; // 上传类型
  int32_t device_id;
  int32_t task_id;
  // 手动任务可能需要回调
  void *callback_ctx; // 指向 cam_shared_ctx_t
} upload_user_ctx_t;

// 注册任务
void uploader_task_start(void);

#endif // !__S#_HELPER__
