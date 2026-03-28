#ifndef CHAT_SERVICE_H
#define CHAT_SERVICE_H

#include "chat_fifo.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum { CHAT_HOME, CHAT_LIVE, CHAT_HISTORY } chat_state_t;

#ifndef CHAT_WINDOW_SIZE
#define CHAT_WINDOW_SIZE 5
#endif

void chat_service_init(void);
void chat_service_loop(void);

void chat_enter_live(void);
void chat_enter_history(uint32_t last_id, uint8_t direction);

void chat_send_text(const char *text);

chat_fifo_t *chat_get_window(void);
chat_state_t chat_get_state(void);

void chat_window_clear_dirty(void);
bool chat_window_is_dirty(void);
#endif
