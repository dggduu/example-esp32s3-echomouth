// chat_service.c
#include "chat_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "net_adapter.h"
#include "protocol.h"
#include <stdio.h>
#include <string.h>


/* ---------- 静态变量 ---------- */
static chat_fifo_t s_window;
static chat_state_t s_state = CHAT_HOME;

static uint32_t s_latest_server_msg_id = 0;
static uint32_t s_current_window_newest = 0;
static bool s_window_dirty = false;

/* 批次接收缓存 */
static msg_t s_batch[CHAT_WINDOW_SIZE];
static int s_batch_count = 0;

/* ACK 相关 */
static uint8_t s_current_epoch = 0;

/* 队列与任务 */
static QueueHandle_t g_packet_queue = NULL;
static TaskHandle_t g_chat_task_handle = NULL;

/* ---------- 内部函数声明 ---------- */
static void send_ack(uint8_t ack_type, uint8_t epoch);
static void apply_batch(void);
static void chat_service_task(void *param);

/* ---------- 初始化 ---------- */
void chat_service_init(void) {
  chat_fifo_init(&s_window);

  if (g_packet_queue == NULL) {
    g_packet_queue = xQueueCreate(20, sizeof(protocol_packet_t));
  }

  if (g_chat_task_handle == NULL) {
    xTaskCreate(chat_service_task, "chat_srv", 4096, NULL, 4,
                &g_chat_task_handle);
  }
}

/* ---------- 供全局任务调用 ---------- */
void chat_service_handle_packet(const protocol_packet_t *pkt) {
  if (g_packet_queue && pkt) {
    xQueueSend(g_packet_queue, pkt, 0);
  }
}

/* ---------- ACK 发送 ---------- */
static void send_ack(uint8_t ack_type, uint8_t epoch) {
  uint8_t buf[32];
  int len = encode_ack_packet(buf, ack_type, epoch);
  net_ws_send(buf, len);
}

/* ---------- 应用批次数据到窗口 ---------- */
static void apply_batch(void) {
  chat_fifo_clear(&s_window);

  for (int i = 0; i < s_batch_count; i++) {
    chat_fifo_push(&s_window, &s_batch[i]);
    if (s_batch[i].msg_id > s_latest_server_msg_id) {
      s_latest_server_msg_id = s_batch[i].msg_id;
    }
  }

  if (s_batch_count > 0) {
    s_current_window_newest = s_batch[s_batch_count - 1].msg_id;
  }

  s_window_dirty = true;
  s_batch_count = 0;

  if (s_current_window_newest == s_latest_server_msg_id) {
    s_state = CHAT_LIVE;
  }
}

/* ---------- 聊天服务内部任务 ---------- */
static void chat_service_task(void *param) {
  protocol_packet_t pkt;

  while (1) {
    if (xQueueReceive(g_packet_queue, &pkt, portMAX_DELAY) == pdTRUE) {
      switch (pkt.type) {
      case TYPE_SYN:
        s_current_epoch = pkt.epoch;
        send_ack(TYPE_SYN, pkt.epoch);
        s_batch_count = 0;
        break;

      case TYPE_DATA:
        if (s_batch_count < CHAT_WINDOW_SIZE) {
          s_batch[s_batch_count].msg_id = pkt.msg_id;
          s_batch[s_batch_count].timestamp = pkt.timestamp;
          s_batch[s_batch_count].sender = pkt.sender;
          strncpy(s_batch[s_batch_count].text, pkt.content, CHAT_TEXT_MAX - 1);
          s_batch[s_batch_count].text[CHAT_TEXT_MAX - 1] = '\0';
          s_batch_count++;
        }
        send_ack(TYPE_DATA, pkt.epoch);
        break;

      case TYPE_END:
        send_ack(TYPE_END, pkt.epoch);
        apply_batch();
        break;

      case TYPE_NOTIFY:
        send_ack(TYPE_NOTIFY, pkt.epoch);
        printf("[CHAT] Received notification, msg_id=%lu\n", pkt.notify_msg_id);
        if (pkt.notify_msg_id > s_latest_server_msg_id) {
          s_latest_server_msg_id = pkt.notify_msg_id;
        }
        // 历史模式下有新消息，可通过回调通知 UI
        break;

      default:
        break;
      }
    }
  }
}

/* ---------- 对外查询接口 ---------- */
chat_fifo_t *chat_get_window(void) { return &s_window; }

chat_state_t chat_get_state(void) { return s_state; }

bool chat_window_is_dirty(void) { return s_window_dirty; }

void chat_window_clear_dirty(void) { s_window_dirty = false; }

/* ---------- 模式切换与请求 ---------- */
void chat_enter_live(void) {
  printf("[CHAT] Entering LIVE mode\n");
  s_state = CHAT_LIVE;
  protocol_request_history(0xFFFFFFFF, HISTORY_DIR_OLDER);
}

void chat_enter_history(uint32_t last_id, uint8_t direction) {
  printf("[CHAT] Entering HISTORY mode, last_id=%lu, direction=%u\n", last_id,
         direction);
  s_state = CHAT_HISTORY;
  protocol_request_history(last_id, direction);
}

/* ---------- 发送文本 ---------- */
void chat_send_text(const char *text) { protocol_send_message(text); }