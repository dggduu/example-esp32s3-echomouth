#ifndef CHAT_SERVICE_H
#define CHAT_SERVICE_H

#include "protocol.h"
#include <stdbool.h>
#include <stdint.h>

#define CHAT_WINDOW_SIZE 5 // 窗口消息数量

typedef struct {
  uint32_t msg_id;
  uint32_t timestamp;
  uint8_t sender; // 1=child, 0=parent
  char text[256];
} msg_t;

// 回调函数类型：UI 渲染通知
typedef void (*chat_render_cb_t)(void);

// 初始化聊天服务
void chat_service_init(void);

// 后台循环（可定期调用，用于维护）
void chat_service_loop(void);

// 处理接收到的协议包
void chat_service_handle_packet(protocol_packet_t *pkt);

// 注册 UI 渲染回调（当新消息到达时调用）
void chat_service_register_render_cb(chat_render_cb_t cb);

// 检查窗口是否有未渲染的新消息
bool chat_window_is_dirty(void);

// 清除脏标记
void chat_window_clear_dirty(void);

// 获取窗口中指定索引的消息（0 为最早，count-1 为最新）
msg_t *chat_fifo_get(int index);

// 获取当前窗口消息数量
int chat_fifo_count(void);

// 发送文本消息（自动分片，每片最大 200 字节）
void chat_send_text(const char *text);

// 进入直播模式
void chat_enter_live(void);

// 请求历史消息
void chat_enter_history(uint32_t last_msg_id, uint8_t direction);

// 退出聊天模式
void chat_exit_chat(void);

// 显示新消息提示（可选）
void chat_show_new_msg_toast(void);

typedef void (*chat_notify_cb_t)(uint32_t msg_id, uint8_t sender,
                                 const char *preview);
void chat_service_register_notify_cb(chat_notify_cb_t cb);

typedef void (*chat_reasoning_cb_t)(const char *message);
void chat_service_register_reasoning_cb(chat_reasoning_cb_t cb);
#endif