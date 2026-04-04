#pragma once
#include <stdbool.h>

typedef struct {
  int task_id;         // [输入] 外部传入的任务ID
  bool is_finished;    // [输出] 上传流程是否结束
  bool success;        // [输出] 是否上传成功
  char image_key[128]; // [输出] 服务器返回的最终 key
} cam_shared_ctx_t;

typedef struct {
  char path[64]; // LittleFS 文件路径
  int task_id;
  cam_shared_ctx_t *ctx; // 指向 UI 传入的上下文
  int retry_count;
} img_job_t;