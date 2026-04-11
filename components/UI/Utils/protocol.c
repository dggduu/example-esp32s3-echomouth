#include "protocol.h"
#include <stdio.h>
#include <string.h>


/* 静态函数声明 */
static uint8_t crc8(const uint8_t *data, int len);
static int build_packet(uint8_t *buf, uint8_t type, uint8_t stream,
                        const uint8_t *payload, uint16_t payload_len,
                        uint8_t epoch);

/* 全局变量 */
static device_mode_t g_current_mode = DEVICE_MODE_HOME;
static int (*g_send_callback)(const uint8_t *data, int len) = NULL;

/* CRC8 计算（与服务端一致） */
static uint8_t crc8(const uint8_t *data, int len) {
  uint8_t crc = 0x00;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = crc & 0x80 ? ((crc << 1) ^ 0x31) : (crc << 1);
    }
  }
  return crc;
}

/* 设置发送回调 */
void protocol_set_send_callback(int (*send_func)(const uint8_t *data,
                                                 int len)) {
  g_send_callback = send_func;
}

/* 连接建立时调用（可重置内部状态） */
void protocol_on_connected(void) {
  // 可在此重置为默认模式
  // g_current_mode = DEVICE_MODE_HOME;
}

/* 连接断开时调用 */
void protocol_on_disconnected(void) {
  // 清理资源（如有需要）
}

/* 获取当前模式 */
device_mode_t protocol_get_mode(void) { return g_current_mode; }

/* 切换本地模式（不发送网络包） */
void protocol_switch_mode(device_mode_t new_mode) { g_current_mode = new_mode; }

/* 发送模式切换请求到服务端 */
bool protocol_send_mode_switch(device_mode_t mode) {
  uint8_t buf[32];
  uint8_t mode_byte = (mode == DEVICE_MODE_CHAT_LIVE) ? 0x01 : 0x00;
  int len =
      build_packet(buf, TYPE_MODE_SWITCH, STREAM_CONTROL, &mode_byte, 1, 0);
  if (g_send_callback) {
    return g_send_callback(buf, len) == len;
  }
  return false;
}

/* 发送聊天消息 */
bool protocol_send_message(const char *text) {
  uint8_t payload[512];
  uint32_t msg_id = 2; // 客户端暂用固定值，实际应由上层生成
  uint8_t part_idx = 0;
  uint8_t total_parts = 1;
  uint8_t sender = 1; // child

  memcpy(payload, &msg_id, 4);
  payload[4] = part_idx;
  payload[5] = total_parts;
  payload[6] = sender;

  int text_len = strlen(text);
  if (text_len > (int)sizeof(payload) - 7)
    text_len = sizeof(payload) - 7;
  memcpy(&payload[7], text, text_len);

  int len =
      build_packet(NULL, TYPE_DATA, STREAM_CHAT, payload, 7 + text_len, 0);
  uint8_t buf[len];
  build_packet(buf, TYPE_DATA, STREAM_CHAT, payload, 7 + text_len, 0);

  if (g_send_callback) {
    return g_send_callback(buf, len) == len;
  }
  return false;
}

/* 请求历史消息 */
bool protocol_request_history(uint32_t last_msg_id, uint8_t direction) {
  uint8_t payload[5];
  memcpy(payload, &last_msg_id, 4);
  payload[4] = direction;

  int len = build_packet(NULL, TYPE_HISTORY_REQ, STREAM_CONTROL, payload, 5, 0);
  uint8_t buf[len];
  build_packet(buf, TYPE_HISTORY_REQ, STREAM_CONTROL, payload, 5, 0);

  if (g_send_callback) {
    return g_send_callback(buf, len) == len;
  }
  return false;
}

/* 构建数据包（内部使用） */
static int build_packet(uint8_t *buf, uint8_t type, uint8_t stream,
                        const uint8_t *payload, uint16_t payload_len,
                        uint8_t epoch) {
  uint8_t *p = buf;
  *p++ = STX;
  *p++ = type;
  *p++ = stream;
  *p++ = epoch;

  uint32_t ts = 0; // 时间戳由上层填入，此处为0
  memcpy(p, &ts, 4);
  p += 4;

  memcpy(p, &payload_len, 2);
  p += 2;

  if (payload_len > 0) {
    memcpy(p, payload, payload_len);
    p += payload_len;
  }

  int total = p - buf;
  buf[total] = crc8(buf, total);
  return total + 1;
}

/* 编码 ACK 包 */
int encode_ack_packet(uint8_t *buf, uint8_t ack_type, uint8_t epoch) {
  uint8_t payload[2] = {ack_type, epoch};
  return build_packet(buf, TYPE_ACK, STREAM_CONTROL, payload, 2, epoch);
}

/* 发送 ACK 响应 */
bool protocol_send_ack(uint8_t ack_type, uint8_t epoch) {
  int len = build_packet(NULL, TYPE_ACK, STREAM_CONTROL, NULL, 0, epoch);
  uint8_t buf[len];
  len = encode_ack_packet(buf, ack_type, epoch);

  if (g_send_callback) {
    return g_send_callback(buf, len) == len;
  }
  return false;
}

/* 解析接收到的数据包 */
bool decode_packet(const uint8_t *buf, int len, protocol_packet_t *out) {
  if (len < 11)
    return false;
  if (buf[0] != STX)
    return false;

  uint8_t calc = crc8(buf, len - 1);
  if (calc != buf[len - 1])
    return false;

  memset(out, 0, sizeof(protocol_packet_t));

  out->type = buf[1];
  out->stream = buf[2];
  out->epoch = buf[3];
  memcpy(&out->timestamp, &buf[4], 4);
  memcpy(&out->payload_len, &buf[8], 2);

  const uint8_t *payload = &buf[10];
  out->payload = payload;

  switch (out->type) {
  case TYPE_DATA: {
    if (out->payload_len < 7)
      return false;
    out->msg_id = *(uint32_t *)&payload[0];
    out->part_idx = payload[4];
    out->total_parts = payload[5];
    out->sender = payload[6];

    int text_len = out->payload_len - 7;
    if (text_len > 0 && text_len < (int)sizeof(out->content)) {
      memcpy(out->content, &payload[7], text_len);
      out->content[text_len] = '\0';
    } else {
      out->content[0] = '\0';
    }
    break;
  }
  case TYPE_NOTIFY: {
    if (out->payload_len < 5)
      return false;
    out->notify_msg_id = *(uint32_t *)&payload[0];
    out->notify_sender = payload[4];
    int preview_len = out->payload_len - 5;
    if (preview_len > 0 && preview_len < (int)sizeof(out->notify_preview)) {
      memcpy(out->notify_preview, &payload[5], preview_len);
      out->notify_preview[preview_len] = '\0';
    } else {
      out->notify_preview[0] = '\0';
    }
    break;
  }
  case TYPE_REASONING: {
    out->reasoning_part_idx =
        out->epoch; // 注意：服务端将 partIdx 放在 epoch 字段？需确认协议
    // 实际根据服务端实现，partIdx/total 可能在其他位置，这里先按 payload
    // 全部为文本处理
    int text_len = out->payload_len;
    if (text_len > 0 && text_len < (int)sizeof(out->reasoning_content)) {
      memcpy(out->reasoning_content, payload, text_len);
      out->reasoning_content[text_len] = '\0';
    } else {
      out->reasoning_content[0] = '\0';
    }
    break;
  }
  case TYPE_ACK: {
    if (out->payload_len >= 2) {
      out->ack_type = payload[0];
      out->ack_epoch = payload[1];
    }
    break;
  }
  case TYPE_MODE_SWITCH: {
    if (out->payload_len >= 1) {
      out->mode_switch_value = payload[0];
    }
    break;
  }
  case TYPE_CMD: {
    if (out->payload_len >= 1) {
      out->cmd_value = payload[0];
    }
    break;
  }
  case TYPE_SYN:
  case TYPE_END:
    // 无需额外解析
    break;
  default:
    break;
  }

  return true;
}

/* 默认弱回调，用户需覆盖实现 */
__attribute__((weak)) void
protocol_on_packet_received(const protocol_packet_t *pkt) {
  // 用户应提供此函数的强实现，处理接收到的数据包
  // 示例：根据包类型执行相应动作
  (void)pkt;
}