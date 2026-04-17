#ifndef CAM_SHARED_H
#define CAM_SHARED_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdbool.h>

typedef struct {
  int task_id;
  bool is_finished;
  bool success;
  int device_id;
  char image_key[128];
  SemaphoreHandle_t done_sem; // 用于同步等待上传完成
  void *page_args;
} cam_shared_ctx_t;

typedef struct {
  int task_id;
  int device_id;
} cam_page_args_t;

#endif