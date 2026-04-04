#include "img_stack.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#define MAX_IMG_STACK 3

static img_job_t stack[MAX_IMG_STACK];
static int top = -1;
static SemaphoreHandle_t mutex;

void img_stack_init(void) {
  mutex = xSemaphoreCreateMutex();
  top = -1;
}

bool img_stack_push(const img_job_t *job) {
  if (!mutex)
    return false;
  xSemaphoreTake(mutex, portMAX_DELAY);

  if (top >= MAX_IMG_STACK - 1) {
    xSemaphoreGive(mutex);
    return false;
  }

  top++;
  memcpy(&stack[top], job, sizeof(img_job_t)); // 拷贝整个任务上下文

  xSemaphoreGive(mutex);
  return true;
}

bool img_stack_peek(img_job_t *out_job) {
  if (!mutex)
    return false;
  xSemaphoreTake(mutex, portMAX_DELAY);

  if (top < 0) {
    xSemaphoreGive(mutex);
    return false;
  }

  memcpy(out_job, &stack[top], sizeof(img_job_t)); // 仅复制读取，不移动指针

  xSemaphoreGive(mutex);
  return true;
}

bool img_stack_commit(void) {
  if (!mutex)
    return false;
  xSemaphoreTake(mutex, portMAX_DELAY);

  if (top >= 0) {
    top--; // 仅在确认成功后移动指针
  }

  xSemaphoreGive(mutex);
  return true;
}

void img_stack_update_retry(const img_job_t *job) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (top >= 0) {
    stack[top].retry_count = job->retry_count;
  }
  xSemaphoreGive(mutex);
}