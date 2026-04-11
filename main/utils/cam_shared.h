#ifndef CAM_SHARED_H
#define CAM_SHARED_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdbool.h>


typedef struct {
  int task_id;
  bool is_finished;
  bool success;
  char image_key[128];
  SemaphoreHandle_t done_sem; // 用于同步等待上传完成
} cam_shared_ctx_t;

#endif