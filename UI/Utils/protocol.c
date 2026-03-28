#include "protocol.h"
#include <string.h>

/* CRC8 same as server */
uint8_t crc8(const uint8_t *data, int len) {
  uint8_t crc = 0x00;

  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x80)
        crc = (crc << 1) ^ 0x31;
      else
        crc <<= 1;
    }
  }
  return crc;
}

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
  out->timestamp = *(uint32_t *)&buf[4];

  uint16_t payload_len = *(uint16_t *)&buf[8];
  const uint8_t *payload = &buf[10];

  if (out->type == TYPE_DATA) {

    out->msg_id = *(uint32_t *)&payload[0];
    out->sender = payload[6];

    int text_len = payload_len - 7;
    if (text_len > 0 && text_len < 256) {
      memcpy(out->content, &payload[7], text_len);
      out->content[text_len] = 0;
    }
  } else if (out->type == TYPE_NOTIFY) {

    out->notify_msg_id = *(uint32_t *)&payload[0];
  }

  return true;
}

static int build_packet(uint8_t *buf, uint8_t type, uint8_t stream,
                        const uint8_t *payload, uint16_t payload_len) {
  buf[0] = STX;
  buf[1] = type;
  buf[2] = stream;
  buf[3] = 0; // epoch 先固定0

  uint32_t ts = 0;
  memcpy(&buf[4], &ts, 4);

  memcpy(&buf[8], &payload_len, 2);

  if (payload_len > 0)
    memcpy(&buf[10], payload, payload_len);

  int total = 10 + payload_len;
  buf[total] = crc8(buf, total);

  return total + 1;
}

int encode_mode_switch_packet(uint8_t *buf, uint8_t mode) {
  return build_packet(buf, TYPE_MODE_SWITCH, STREAM_CONTROL, &mode, 1);
}

int encode_history_req(uint8_t *buf, uint32_t last_id, uint8_t direction) {
  uint8_t payload[5];

  memcpy(payload, &last_id, 4);
  payload[4] = direction;

  return build_packet(buf, TYPE_HISTORY_REQ, STREAM_CONTROL, payload, 5);
}

int encode_data_packet(uint8_t *buf, const char *text) {
  uint8_t payload[512];

  uint32_t msg_id = 2; // 不能写0
  memcpy(payload, &msg_id, 4);

  payload[4] = 0;
  payload[5] = 1;
  payload[6] = 1; // child

  int text_len = strlen(text);
  memcpy(&payload[7], text, text_len);

  return build_packet(buf, TYPE_DATA, STREAM_CHAT, payload, 7 + text_len);
}

int encode_ack_packet(uint8_t *buf, uint8_t ack_type, uint8_t epoch) {
  uint8_t payload[2];
  payload[0] = ack_type; // 被确认的包类型
  payload[1] = epoch;    // 被确认的 epoch

  buf[0] = STX;
  buf[1] = TYPE_ACK;
  buf[2] = STREAM_CONTROL;
  buf[3] = epoch;

  uint32_t ts = 0;
  memcpy(&buf[4], &ts, 4);

  uint16_t payload_len = 2;
  memcpy(&buf[8], &payload_len, 2);
  memcpy(&buf[10], payload, 2);

  int total = 12;
  buf[total] = crc8(buf, total);

  return total + 1;
}
