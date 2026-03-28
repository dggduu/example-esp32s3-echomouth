#include "chat_fifo.h"

void chat_fifo_init(chat_fifo_t *fifo) { memset(fifo, 0, sizeof(chat_fifo_t)); }

void chat_fifo_clear(chat_fifo_t *fifo) {
  fifo->head = 0;
  fifo->count = 0;
}

void chat_fifo_push(chat_fifo_t *fifo, const msg_t *msg) {
  if (fifo->count < FIFO_SIZE) {
    int pos = (fifo->head + fifo->count) % FIFO_SIZE;
    fifo->buf[pos] = *msg;
    fifo->count++;
  } else {
    /* 覆盖最老 */
    fifo->buf[fifo->head] = *msg;
    fifo->head = (fifo->head + 1) % FIFO_SIZE;
  }
}

void chat_fifo_replace(chat_fifo_t *fifo, const msg_t *src, int src_cnt) {
  chat_fifo_clear(fifo);

  int n = src_cnt > FIFO_SIZE ? FIFO_SIZE : src_cnt;

  for (int i = 0; i < n; i++) {
    fifo->buf[i] = src[i];
  }

  fifo->head = 0;
  fifo->count = n;
}

msg_t *chat_fifo_get(chat_fifo_t *fifo, int idx) {
  if (idx >= fifo->count)
    return NULL;

  int pos = (fifo->head + idx) % FIFO_SIZE;
  return &fifo->buf[pos];
}

msg_t *chat_fifo_get_latest(chat_fifo_t *fifo) {
  if (fifo->count == 0)
    return NULL;

  int pos = (fifo->head + fifo->count - 1) % FIFO_SIZE;
  return &fifo->buf[pos];
}

msg_t *chat_fifo_get_oldest(chat_fifo_t *fifo) {
  if (fifo->count == 0)
    return NULL;
  return &fifo->buf[fifo->head];
}

int chat_fifo_count(chat_fifo_t *fifo) { return fifo->count; }
