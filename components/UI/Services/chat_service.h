#ifndef CHAT_SERVICE_H
#define CHAT_SERVICE_H

#include "protocol.h"
#include <stdbool.h>
#include <stdint.h>

#define CHAT_WINDOW_SIZE 5

typedef struct {
  uint32_t msg_id;
  uint32_t timestamp;
  uint8_t sender;
  char text[256];
} msg_t;

typedef void (*chat_render_cb_t)(void);

void chat_service_init(void);

void chat_service_loop(void);

void chat_service_handle_packet(protocol_packet_t *pkt);

void chat_service_register_render_cb(chat_render_cb_t cb);

bool chat_window_is_dirty(void);

void chat_window_clear_dirty(void);

msg_t *chat_fifo_get(int index);

int chat_fifo_count(void);

void chat_send_text(const char *text);

void chat_enter_live(void);

void chat_enter_history(uint32_t last_msg_id, uint8_t direction);

void chat_exit_chat(void);

void chat_show_new_msg_toast(void);

typedef void (*chat_notify_cb_t)(uint32_t msg_id, uint8_t sender,
                                 const char *preview);
void chat_service_register_notify_cb(chat_notify_cb_t cb);

typedef void (*chat_reasoning_cb_t)(const char *message);
void chat_service_register_reasoning_cb(chat_reasoning_cb_t cb);
#endif