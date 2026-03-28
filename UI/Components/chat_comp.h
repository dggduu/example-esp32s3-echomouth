#ifndef CHAT_COMP_H
#define CHAT_COMP_H

#include "chat_fifo.h"
#include "lvgl.h"

lv_obj_t *chat_comp_create(lv_obj_t *parent);
void chat_show_new_msg_toast(void);
void chat_comp_loop(void);
#endif