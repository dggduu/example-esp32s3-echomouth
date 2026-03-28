#ifndef CHAT_FIFO_H
#define CHAT_FIFO_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define FIFO_SIZE 5
#define CHAT_TEXT_MAX 256
#define CHAT_WINDOW_SIZE FIFO_SIZE

typedef struct {
  uint32_t msg_id;
  uint32_t timestamp;
  uint8_t sender; // 0 parent 1 child
  char text[CHAT_TEXT_MAX];
} msg_t;

typedef struct {
  msg_t buf[FIFO_SIZE];
  int head;  // 指向最老
  int count; // 当前数量
} chat_fifo_t;

/* 初始化 */
void chat_fifo_init(chat_fifo_t *fifo);

/* push（覆盖式环形） */
void chat_fifo_push(chat_fifo_t *fifo, const msg_t *msg);

/* 批量替换（用于SYN→DATA→END） */
void chat_fifo_replace(chat_fifo_t *fifo, const msg_t *src, int src_cnt);

/* 清空 */
void chat_fifo_clear(chat_fifo_t *fifo);

/* 获取 */
msg_t *chat_fifo_get(chat_fifo_t *fifo, int idx);
msg_t *chat_fifo_get_latest(chat_fifo_t *fifo);
msg_t *chat_fifo_get_oldest(chat_fifo_t *fifo);

int chat_fifo_count(chat_fifo_t *fifo);

#endif
