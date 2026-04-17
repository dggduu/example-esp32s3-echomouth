#include "img_queue.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>

#define MAX_QUEUE_SIZE 5

typedef struct {
  img_job_t jobs[MAX_QUEUE_SIZE];
  int count;
  SemaphoreHandle_t mutex;
} img_queue_t;

static img_queue_t s_queue;

void img_queue_init(void) {
  memset(&s_queue, 0, sizeof(s_queue));
  s_queue.mutex = xSemaphoreCreateMutex();
}

static bool insert_sorted(const img_job_t *job) {
  if (s_queue.count >= MAX_QUEUE_SIZE)
    return false;

  int insert_pos = 0;

  while (insert_pos < s_queue.count &&
         s_queue.jobs[insert_pos].priority <= job->priority) {
    insert_pos++;
  }

  for (int i = s_queue.count; i > insert_pos; i--) {
    memcpy(&s_queue.jobs[i], &s_queue.jobs[i - 1], sizeof(img_job_t));
  }

  memcpy(&s_queue.jobs[insert_pos], job, sizeof(img_job_t));
  s_queue.count++;
  return true;
}

bool img_queue_push(const img_job_t *job) {
  if (!s_queue.mutex || !job)
    return false;

  xSemaphoreTake(s_queue.mutex, portMAX_DELAY);
  bool ret = insert_sorted(job);
  xSemaphoreGive(s_queue.mutex);
  return ret;
}

bool img_queue_peek(img_job_t *out_job) {
  if (!s_queue.mutex || !out_job)
    return false;

  xSemaphoreTake(s_queue.mutex, portMAX_DELAY);

  if (s_queue.count == 0) {
    xSemaphoreGive(s_queue.mutex);
    return false;
  }

  memcpy(out_job, &s_queue.jobs[0], sizeof(img_job_t));
  xSemaphoreGive(s_queue.mutex);
  return true;
}

bool img_queue_commit(void) {
  if (!s_queue.mutex)
    return false;

  xSemaphoreTake(s_queue.mutex, portMAX_DELAY);

  if (s_queue.count == 0) {
    xSemaphoreGive(s_queue.mutex);
    return false;
  }

  for (int i = 0; i < s_queue.count - 1; i++) {
    memcpy(&s_queue.jobs[i], &s_queue.jobs[i + 1], sizeof(img_job_t));
  }
  s_queue.count--;

  xSemaphoreGive(s_queue.mutex);
  return true;
}

void img_queue_update_retry(uint8_t new_retry_count) {
  if (!s_queue.mutex)
    return;

  xSemaphoreTake(s_queue.mutex, portMAX_DELAY);
  if (s_queue.count > 0) {
    s_queue.jobs[0].retry_count = new_retry_count;
  }
  xSemaphoreGive(s_queue.mutex);
}

int img_queue_get_count(void) {
  int count = 0;
  if (s_queue.mutex) {
    xSemaphoreTake(s_queue.mutex, portMAX_DELAY);
    count = s_queue.count;
    xSemaphoreGive(s_queue.mutex);
  }
  return count;
}

bool img_queue_is_full(void) {
  bool full = false;
  if (s_queue.mutex) {
    xSemaphoreTake(s_queue.mutex, portMAX_DELAY);
    full = (s_queue.count >= MAX_QUEUE_SIZE);
    xSemaphoreGive(s_queue.mutex);
  }
  return full;
}