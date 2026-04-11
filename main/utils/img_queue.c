#include "img_queue.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>


#define MAX_QUEUE_SIZE 5  // 队列容量，可根据 RAM 调整
#define MAX_RETRY_LIMIT 5 // 最大重试次数

typedef struct {
  img_job_t jobs[MAX_QUEUE_SIZE];
  int head;  // 取任务位置
  int tail;  // 存任务位置
  int count; // 当前任务数
  SemaphoreHandle_t mutex;
} img_queue_t;

static img_queue_t s_queue;

/* ---------- 初始化 ---------- */
void img_queue_init(void) {
  memset(&s_queue, 0, sizeof(s_queue));
  s_queue.mutex = xSemaphoreCreateMutex();
  // head/tail/count 均为 0
}

/* ---------- 入队（FIFO，但按优先级重新排序） ---------- */
bool img_queue_push(const img_job_t *job) {
  if (!s_queue.mutex || !job)
    return false;

  xSemaphoreTake(s_queue.mutex, portMAX_DELAY);

  if (s_queue.count >= MAX_QUEUE_SIZE) {
    xSemaphoreGive(s_queue.mutex);
    return false;
  }

  // 直接放入尾部
  memcpy(&s_queue.jobs[s_queue.tail], job, sizeof(img_job_t));
  s_queue.tail = (s_queue.tail + 1) % MAX_QUEUE_SIZE;
  s_queue.count++;

  // 按优先级重新排序（冒泡，因为队列很小，性能无影响）
  // 优先级高的排在前面（head 方向）
  for (int i = 0; i < s_queue.count - 1; i++) {
    for (int j = 0; j < s_queue.count - 1 - i; j++) {
      int idx1 = (s_queue.head + j) % MAX_QUEUE_SIZE;
      int idx2 = (s_queue.head + j + 1) % MAX_QUEUE_SIZE;
      if (s_queue.jobs[idx1].priority < s_queue.jobs[idx2].priority) {
        // 交换
        img_job_t tmp;
        memcpy(&tmp, &s_queue.jobs[idx1], sizeof(img_job_t));
        memcpy(&s_queue.jobs[idx1], &s_queue.jobs[idx2], sizeof(img_job_t));
        memcpy(&s_queue.jobs[idx2], &tmp, sizeof(img_job_t));
      }
    }
  }

  xSemaphoreGive(s_queue.mutex);
  return true;
}

/* ---------- 查看队首任务（不移除） ---------- */
bool img_queue_peek(img_job_t *out_job) {
  if (!s_queue.mutex || !out_job)
    return false;

  xSemaphoreTake(s_queue.mutex, portMAX_DELAY);

  if (s_queue.count == 0) {
    xSemaphoreGive(s_queue.mutex);
    return false;
  }

  // 跳过重试次数超限的任务（死信）
  while (s_queue.count > 0 &&
         s_queue.jobs[s_queue.head].retry_count >= MAX_RETRY_LIMIT) {
    // 调用失败回调（如果有）
    img_job_t *dead = &s_queue.jobs[s_queue.head];
    if (dead->on_complete) {
      dead->on_complete(false, NULL, dead->user_data);
    }
    // 删除本地文件
    remove(dead->path);
    // 弹出死信
    s_queue.head = (s_queue.head + 1) % MAX_QUEUE_SIZE;
    s_queue.count--;
  }

  if (s_queue.count == 0) {
    xSemaphoreGive(s_queue.mutex);
    return false;
  }

  memcpy(out_job, &s_queue.jobs[s_queue.head], sizeof(img_job_t));
  xSemaphoreGive(s_queue.mutex);
  return true;
}

/* ---------- 提交（移除队首） ---------- */
bool img_queue_commit(void) {
  if (!s_queue.mutex)
    return false;

  xSemaphoreTake(s_queue.mutex, portMAX_DELAY);

  if (s_queue.count == 0) {
    xSemaphoreGive(s_queue.mutex);
    return false;
  }

  s_queue.head = (s_queue.head + 1) % MAX_QUEUE_SIZE;
  s_queue.count--;

  xSemaphoreGive(s_queue.mutex);
  return true;
}

/* ---------- 更新当前队首任务的重试次数 ---------- */
void img_queue_update_retry(uint8_t new_retry_count) {
  if (!s_queue.mutex)
    return;

  xSemaphoreTake(s_queue.mutex, portMAX_DELAY);

  if (s_queue.count > 0) {
    s_queue.jobs[s_queue.head].retry_count = new_retry_count;
  }

  xSemaphoreGive(s_queue.mutex);
}

/* ---------- 获取队列大小 ---------- */
int img_queue_get_count(void) {
  int count = 0;
  if (s_queue.mutex) {
    xSemaphoreTake(s_queue.mutex, portMAX_DELAY);
    count = s_queue.count;
    xSemaphoreGive(s_queue.mutex);
  }
  return count;
}

/* ---------- 队列是否已满 ---------- */
bool img_queue_is_full(void) {
  bool full = false;
  if (s_queue.mutex) {
    xSemaphoreTake(s_queue.mutex, portMAX_DELAY);
    full = (s_queue.count >= MAX_QUEUE_SIZE);
    xSemaphoreGive(s_queue.mutex);
  }
  return full;
}