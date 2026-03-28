#include "chat_service.h"
#include "net_adapter.h"
#include "protocol.h"
#include <stdio.h>
#include <string.h>

static chat_fifo_t s_window;
static chat_state_t s_state = CHAT_HOME;

static uint32_t s_latest_server_msg_id = 0;
static uint32_t s_current_window_newest = 0;
static bool s_window_dirty = false;

/* 临时接收缓存 */
static msg_t s_batch[CHAT_WINDOW_SIZE];
static int s_batch_count = 0;

chat_fifo_t *chat_get_window(void) { return &s_window; }

chat_state_t chat_get_state(void) { return s_state; }

void chat_service_init(void) { chat_fifo_init(&s_window); }

bool chat_window_is_dirty(void) { return s_window_dirty; }

void chat_window_clear_dirty(void) { s_window_dirty = false; }

/* ------------------ 协议发送 ------------------ */

static void send_mode_switch(uint8_t mode) {

  printf("[SERVICE] Switching mode to %u\n", mode);
  uint8_t buf[32];
  size_t len = encode_mode_switch_packet(buf, mode); // 你已有协议封装
  net_ws_send(buf, len);
}

static void request_latest_5(void) {
  uint8_t buf[32];
  size_t len = encode_history_req(buf, 0xffffffff, 0);
  net_ws_send(buf, len);
}

static void request_history(uint32_t last_id, uint8_t dir) {
  uint8_t buf[32];
  size_t len = encode_history_req(buf, last_id, dir);
  net_ws_send(buf, len);
}

/* ------------------ 状态切换 ------------------ */

void chat_enter_live(void) {
  printf("[SERVICE] Entering LIVE mode\n");
  s_state = CHAT_LIVE;
  request_latest_5();
  send_mode_switch(0x01);
}

void chat_enter_history(uint32_t last_id, uint8_t direction) {
  printf("[SERVICE] Entering HISTORY mode, last_id=%lu, direction=%u\n",
         last_id, direction);
  s_state = CHAT_HISTORY;
  request_history(last_id, direction);
}

/* ------------------ 发送消息 ------------------ */

void chat_send_text(const char *text) {
  uint8_t buf[512];
  size_t len = encode_data_packet(buf, text);
  net_ws_send(buf, len);
}

/* ------------------ 批量处理 ------------------ */

static void apply_batch(void) {
  chat_fifo_clear(&s_window);

  for (int i = 0; i < s_batch_count; i++) {
    chat_fifo_push(&s_window, &s_batch[i]);
    if (s_batch[i].msg_id > s_latest_server_msg_id)
      s_latest_server_msg_id = s_batch[i].msg_id;
  }

  if (s_batch_count > 0)
    s_current_window_newest = s_batch[s_batch_count - 1].msg_id;

  s_window_dirty = true;
  s_batch_count = 0;

  if (s_current_window_newest == s_latest_server_msg_id)
    s_state = CHAT_LIVE;
}

/* ------------------ ACK ------------------ */
static uint8_t s_current_epoch = 0;

static void send_ack(uint8_t ack_type, uint8_t epoch) {
  uint8_t buf[32];
  int len = encode_ack_packet(buf, ack_type, epoch);
  net_ws_send(buf, len);
}

/* ------------------ 主循环解析 ------------------ */

void chat_service_loop(void) {
  uint8_t buf[1024];
  size_t len;

  while (net_ws_fetch_rx(buf, &len)) {

    protocol_packet_t pkt;
    if (!decode_packet(buf, len, &pkt))
      continue;

    if (pkt.type == TYPE_SYN) {
      s_current_epoch = pkt.epoch;
      send_ack(TYPE_SYN, pkt.epoch);
      s_batch_count = 0;
    } else if (pkt.type == TYPE_DATA) {

      send_ack(TYPE_DATA, pkt.epoch);

      if (s_batch_count < CHAT_WINDOW_SIZE) {
        s_batch[s_batch_count].msg_id = pkt.msg_id;
        s_batch[s_batch_count].timestamp = pkt.timestamp;
        s_batch[s_batch_count].sender = pkt.sender;
        strncpy(s_batch[s_batch_count].text, pkt.content, CHAT_TEXT_MAX - 1);

        s_batch[s_batch_count].text[CHAT_TEXT_MAX - 1] = 0;

        s_batch_count++;
      }
    } else if (pkt.type == TYPE_END) {

      send_ack(TYPE_END, pkt.epoch);
      apply_batch();
    } else if (pkt.type == TYPE_NOTIFY) {

      send_ack(TYPE_NOTIFY, pkt.epoch);

      printf("[SERVICE] Received notification, msg_id=%lu\n",
             pkt.notify_msg_id);

      uint32_t msgId = pkt.notify_msg_id;
      if (msgId > s_latest_server_msg_id)
        s_latest_server_msg_id = msgId;

      if (s_state == CHAT_HISTORY) {
        // 触发 toast
        extern void chat_show_new_msg_toast(void);
        chat_show_new_msg_toast();
      }
    }
  }

  if (!net_is_connected()) {
    chat_enter_live(); // 重连自动回 live
  }
}
