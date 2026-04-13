#include "protocol.h"
#include "esp_log.h"
#include <string.h>

#include "freertos/FreeRTOS.h"

static const char *TAG = "PROTOCOL";
device_mode_t g_current_mode = DEVICE_MODE_HOME;
static portMUX_TYPE g_mode_spinlock = portMUX_INITIALIZER_UNLOCKED;

void protocol_set_mode(device_mode_t mode) {
  portENTER_CRITICAL(&g_mode_spinlock);

  g_current_mode = mode;
  portEXIT_CRITICAL(&g_mode_spinlock);
  ESP_LOGI(TAG, "Mode switch: %d -> %d", g_current_mode, mode);
}

device_mode_t protocol_get_mode(void) {
  device_mode_t mode;
  portENTER_CRITICAL(&g_mode_spinlock);
  mode = g_current_mode;
  portEXIT_CRITICAL(&g_mode_spinlock);
  return mode;
}

static int build_packet(uint8_t *buf, uint8_t type, uint8_t stream,
                        const uint8_t *payload, uint16_t payload_len) {
  buf[0] = STX;
  buf[1] = type;
  buf[2] = stream;
  buf[3] = 0; // epoch 固定为0，服务器会忽略或自行处理
  uint32_t ts = 0;
  memcpy(&buf[4], &ts, 4);
  memcpy(&buf[8], &payload_len, 2);
  if (payload_len > 0)
    memcpy(&buf[10], payload, payload_len);
  int total = 10 + payload_len;
  buf[total] = crc8(buf, total);
  return total + 1;
}

uint8_t crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ CRC8_POLY;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

bool decode_packet(const uint8_t *raw, size_t len, protocol_packet_t *out) {
  if (!raw || len < 11)
    return false;
  if (raw[0] != STX)
    return false;

  uint8_t expected_crc = raw[len - 1];
  if (crc8(raw, len - 1) != expected_crc)
    return false;

  out->type = raw[1];
  out->stream = raw[2];
  out->epoch = raw[3];
  memcpy(&out->timestamp, &raw[4], 4);
  memcpy(&out->payload_len, &raw[8], 2);
  out->payload = &raw[10];

  if (out->type == TYPE_DATA) {
    if (out->payload_len < 7)
      return false;
    memcpy(&out->msg_id, out->payload, 4);
    out->part_idx = out->payload[4];
    out->total_parts = out->payload[5];
    out->sender = out->payload[6];
  } else if (out->type == TYPE_NOTIFY) {
    if (out->payload_len >= 5) {
      out->notify_msg_id = *(uint32_t *)&out->payload[0];
      out->notify_sender = out->payload[4];
      int preview_len = out->payload_len - 5;
      if (preview_len > 0 && preview_len < sizeof(out->notify_preview)) {
        memcpy(out->notify_preview, &out->payload[5], preview_len);
        out->notify_preview[preview_len] = '\0';
      }
    }
  } else if (out->type == TYPE_REASONING) {
    size_t text_len = out->payload_len;
    if (text_len > 0 && text_len < sizeof(out->reasoning_content)) {
      memcpy(out->reasoning_content, out->payload, text_len);
      out->reasoning_content[text_len] = '\0';
    } else {
      out->reasoning_content[0] = '\0';
    }
  }
  return true;
}

int encode_packet(uint8_t *out_buf, size_t out_size, uint8_t type,
                  const uint8_t *payload, size_t payload_len, uint8_t stream,
                  uint8_t epoch, uint32_t timestamp, uint32_t msg_id,
                  uint8_t part_idx, uint8_t total_parts, uint8_t sender) {
  const uint8_t *send_payload = payload;
  size_t send_payload_len = payload_len;
  uint8_t header[7];

  if (type == TYPE_DATA) {
    // 构造 DATA 头（小端序）
    header[0] = msg_id & 0xFF;
    header[1] = (msg_id >> 8) & 0xFF;
    header[2] = (msg_id >> 16) & 0xFF;
    header[3] = (msg_id >> 24) & 0xFF;
    header[4] = part_idx;
    header[5] = total_parts;
    header[6] = sender;
    send_payload = header;
    send_payload_len = 7 + payload_len;
  }

  size_t total_len =
      10 + send_payload_len +
      1; // STX+type+stream+epoch+timestamp(4)+len(2) + payload + CRC
  if (out_size < total_len) {
    ESP_LOGE("PROTOCOL", "Buffer too small");
    return -1;
  }

  out_buf[0] = STX;
  out_buf[1] = type;
  out_buf[2] = stream;
  out_buf[3] = epoch;
  memcpy(&out_buf[4], &timestamp, 4);
  uint16_t len16 = (uint16_t)send_payload_len;
  memcpy(&out_buf[8], &len16, 2);
  if (send_payload_len > 0) {
    memcpy(&out_buf[10], send_payload, send_payload_len);
  }
  out_buf[10 + send_payload_len] = crc8(out_buf, 10 + send_payload_len);

  // 调试打印（可选）
  ESP_LOG_BUFFER_HEX("TX_PACKET", out_buf, total_len);
  return (int)total_len;
}

int encode_ack(uint8_t *out_buf, size_t out_size, uint8_t acked_type,
               uint8_t acked_epoch) {
  uint8_t payload[2] = {acked_type, acked_epoch};
  return encode_packet(out_buf, out_size, TYPE_ACK, payload, 2, STREAM_CONTROL,
                       acked_epoch, 0, 0, 0, 0, 0);
}

int encode_ack_packet(uint8_t *buf, uint8_t acked_type, uint8_t epoch) {
  uint8_t payload[2] = {acked_type, epoch};
  return build_packet(buf, TYPE_ACK, STREAM_CONTROL, payload, 2);
}

int encode_mode_switch(uint8_t *out_buf, size_t out_size, uint8_t mode_byte) {
  return encode_packet(out_buf, out_size, TYPE_MODE_SWITCH, &mode_byte, 1,
                       STREAM_CONTROL, 0, 0, 0, 0, 0, 0);
}

int encode_history_req(uint8_t *out_buf, size_t out_size, uint32_t last_msg_id,
                       uint8_t direction) {
  uint8_t payload[5];
  memcpy(payload, &last_msg_id, 4);
  payload[4] = direction;
  return encode_packet(out_buf, out_size, TYPE_HISTORY_REQ, payload, 5,
                       STREAM_CONTROL, 0, 0, 0, 0, 0, 0);
}

int encode_data_packet(uint8_t *buf, const char *text) {
  uint8_t payload[512];
  uint32_t msg_id = 1; // 不能为0，临时填1
  memcpy(payload, &msg_id, 4);
  payload[4] = 0; // part_idx
  payload[5] = 1; // total_parts = 1（不分片）
  payload[6] = 1; // sender: 1=child, 0=parent
  int text_len = strlen(text);
  memcpy(&payload[7], text, text_len);
  return build_packet(buf, TYPE_DATA, STREAM_CHAT, payload, 7 + text_len);
}