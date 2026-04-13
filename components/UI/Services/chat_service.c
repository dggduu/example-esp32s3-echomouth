#include "chat_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "net_adapter.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "CHAT_SERVICE";

static msg_t s_window[CHAT_WINDOW_SIZE];
static int s_window_count = 0;
static int s_window_head = 0; // 最新消息插入位置
static bool s_dirty = false;
static SemaphoreHandle_t s_fifo_mutex = NULL;

static chat_render_cb_t s_render_cb = NULL;

// 全局模式（定义在 protocol.c 中，声明为 extern）
extern device_mode_t g_current_mode;

static chat_notify_cb_t s_notify_cb = NULL;

void chat_service_register_notify_cb(chat_notify_cb_t cb) { s_notify_cb = cb; }
/* ---------- 内部函数 ---------- */
static void add_message_to_window(uint32_t msg_id, uint32_t timestamp,
                                  uint8_t sender, const char *text) {
  if (!text || strlen(text) == 0)
    return;
  xSemaphoreTake(s_fifo_mutex, portMAX_DELAY);
  msg_t *slot = &s_window[s_window_head];
  slot->msg_id = msg_id;
  slot->timestamp = timestamp;
  slot->sender = sender;
  strncpy(slot->text, text, sizeof(slot->text) - 1);
  slot->text[sizeof(slot->text) - 1] = '\0';

  s_window_head = (s_window_head + 1) % CHAT_WINDOW_SIZE;
  if (s_window_count < CHAT_WINDOW_SIZE)
    s_window_count++;
  s_dirty = true;
  xSemaphoreGive(s_fifo_mutex);
  ESP_LOGI(TAG, "Window updated: %s", text);

  // 触发 UI 刷新回调
  if (s_render_cb) {
    s_render_cb();
  }
}

static void handle_data_packet(protocol_packet_t *pkt) {
  ESP_LOGI(TAG, "DATA: msg_id=%lu, part=%d/%d, sender=%d, payload_len=%d",
           pkt->msg_id, pkt->part_idx, pkt->total_parts, pkt->sender,
           pkt->payload_len);
  if (pkt->msg_id == 0) {
    ESP_LOGW(TAG, "DATA with msg_id=0, ignoring");
    return;
  }

  // 提取文本内容（跳过 DATA 头 7 字节）
  size_t content_len = pkt->payload_len - 7;
  if (content_len == 0) {
    ESP_LOGW(TAG, "DATA with empty content, ignoring");
    return;
  }

  char *content = malloc(content_len + 1);
  if (!content) {
    ESP_LOGE(TAG, "Out of memory for content");
    return;
  }
  memcpy(content, pkt->payload + 7, content_len);
  content[content_len] = '\0';

  add_message_to_window(pkt->msg_id, pkt->timestamp, pkt->sender, content);
  free(content);
}

/* ---------- 公共接口 ---------- */
void chat_service_init(void) {
  s_fifo_mutex = xSemaphoreCreateMutex();
  memset(s_window, 0, sizeof(s_window));
  s_window_count = 0;
  s_window_head = 0;
  s_dirty = false;
  // 初始模式已在 protocol.c 中设置为 DEVICE_MODE_HOME
}

void chat_service_loop(void) {
  // 可保留一些周期性维护，例如心跳打印，但不再用于渲染
  static uint32_t last_print = 0;
  uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
  if (now - last_print > 5000) {
    last_print = now;
  }
}

void chat_service_handle_packet(protocol_packet_t *pkt) {
  switch (pkt->type) {
  case TYPE_DATA:
    handle_data_packet(pkt);
    break;
  case TYPE_SYN:
    ESP_LOGI(TAG, "SYN epoch=%d", pkt->epoch);
    break;
  case TYPE_END:
    ESP_LOGI(TAG, "END epoch=%d", pkt->epoch);
    break;
  case TYPE_NOTIFY:
    ESP_LOGI(TAG, "NOTIFY: msg_id=%lu, sender=%d, preview=%s",
             pkt->notify_msg_id, pkt->notify_sender, pkt->notify_preview);
    if (s_notify_cb) {
      s_notify_cb(pkt->notify_msg_id, pkt->notify_sender, pkt->notify_preview);
    }
    break;
  default:
    break;
  }
}

void chat_service_register_render_cb(chat_render_cb_t cb) { s_render_cb = cb; }

bool chat_window_is_dirty(void) {
  bool dirty;
  xSemaphoreTake(s_fifo_mutex, portMAX_DELAY);
  dirty = s_dirty;
  xSemaphoreGive(s_fifo_mutex);
  return dirty;
}

void chat_window_clear_dirty(void) {
  xSemaphoreTake(s_fifo_mutex, portMAX_DELAY);
  s_dirty = false;
  xSemaphoreGive(s_fifo_mutex);
}

msg_t *chat_fifo_get(int index) {
  msg_t *msg = NULL;
  xSemaphoreTake(s_fifo_mutex, portMAX_DELAY);
  if (index < s_window_count) {
    int pos = (s_window_head - 1 - index + CHAT_WINDOW_SIZE) % CHAT_WINDOW_SIZE;
    msg = &s_window[pos];
  }
  xSemaphoreGive(s_fifo_mutex);
  return msg;
}

int chat_fifo_count(void) {
  int count;
  xSemaphoreTake(s_fifo_mutex, portMAX_DELAY);
  count = s_window_count;
  xSemaphoreGive(s_fifo_mutex);
  return count;
}

void chat_enter_live(void) {
  protocol_set_mode(DEVICE_MODE_CHAT_LIVE);
  uint8_t buf[32];
  int len = encode_mode_switch(buf, sizeof(buf), 0x01);
  if (len > 0)
    net_ws_send(buf, len);
  ESP_LOGI(TAG, "Enter LIVE mode");
}

void chat_enter_history(uint32_t last_msg_id, uint8_t direction) {
  protocol_set_mode(DEVICE_MODE_CHAT_HISTORY);
  uint8_t buf[32];
  int len = encode_history_req(buf, sizeof(buf), last_msg_id, direction);
  if (len > 0)
    net_ws_send(buf, len);
  ESP_LOGI(TAG, "Request history last_id=%lu dir=%d", last_msg_id, direction);
}

void chat_exit_chat(void) {
  protocol_set_mode(DEVICE_MODE_HOME);
  uint8_t buf[32];
  int len = encode_mode_switch(buf, sizeof(buf), 0x00);
  if (len > 0)
    net_ws_send(buf, len);
  ESP_LOGI(TAG, "Exit to HOME mode");
}

void chat_send_text(const char *text) {
  if (!text)
    return;
  uint8_t buf[512];
  int len = encode_data_packet(buf, text);
  if (len > 0) {
    net_ws_send(buf, len);
    ESP_LOGI(TAG, "Sent text: %s", text);
  } else {
    ESP_LOGE(TAG, "Failed to encode data packet");
  }
}

void chat_show_new_msg_toast(void) {
  // 可在此实现 toast 提示
  ESP_LOGI(TAG, "New message arrived (toast)");
}
