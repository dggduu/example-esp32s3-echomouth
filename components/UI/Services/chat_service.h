// chat_service.h
#ifndef CHAT_SERVICE_H
#define CHAT_SERVICE_H

#include "chat_fifo.h" // 复用 msg_t 和 chat_fifo_t
#include "protocol.h"
#include <stdbool.h>
#include <stdint.h>


/* 聊天状态 */
typedef enum { CHAT_HOME = 0, CHAT_LIVE, CHAT_HISTORY } chat_state_t;

/* 初始化聊天服务（启动内部任务） */
void chat_service_init(void);

/* 供全局网络接收任务调用，将包推入处理队列 */
void chat_service_handle_packet(const protocol_packet_t *pkt);

/* 获取当前聊天窗口（供 UI 读取） */
chat_fifo_t *chat_get_window(void);

/* 获取当前聊天状态 */
chat_state_t chat_get_state(void);

/* 窗口是否有更新（UI 检查用） */
bool chat_window_is_dirty(void);
void chat_window_clear_dirty(void);

/* 进入实时聊天模式 */
void chat_enter_live(void);

/* 进入历史浏览模式 */
void chat_enter_history(uint32_t last_id, uint8_t direction);

/* 发送聊天文本 */
void chat_send_text(const char *text);

#endif /* CHAT_SERVICE_H */