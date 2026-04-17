#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "stddef.h"
#include <stdbool.h>
#include <stdint.h>

#define STX 0x02
#define CRC8_POLY 0x31

// 协议类型
#define TYPE_MODE_SWITCH 0x00
#define TYPE_CMD 0x01
#define TYPE_DATA 0x02
#define TYPE_REASONING 0x03
#define TYPE_ACK 0x05
#define TYPE_HISTORY_REQ 0x06
#define TYPE_SYN 0x07
#define TYPE_END 0x08
#define TYPE_NOTIFY 0x09

// 数据流类型
#define STREAM_CONTROL 0x00
#define STREAM_CHAT 0x01
#define STREAM_REASONING 0x02

// 历史方向
#define HISTORY_DIR_OLDER 0x00
#define HISTORY_DIR_NEWER 0x01

// 设备模式
typedef enum {
  DEVICE_MODE_HOME,
  DEVICE_MODE_CHAT_LIVE,
  DEVICE_MODE_CHAT_HISTORY
} device_mode_t;

// 解析后的数据包
typedef struct {
  uint8_t type;
  uint8_t stream;
  uint8_t epoch;
  uint32_t timestamp;
  uint16_t payload_len;
  const uint8_t *payload;
  // TYPE_DATA 专用字段
  uint32_t msg_id;
  uint8_t part_idx;
  uint8_t total_parts;
  uint8_t sender; // 1=child, 0=parent

  // NOTIFY 专用
  uint32_t notify_msg_id;
  uint8_t notify_sender;
  char notify_preview[64];
  char reasoning_content[256];
} protocol_packet_t;

// 模式管理
void protocol_set_mode(device_mode_t mode);
device_mode_t protocol_get_mode(void);

// 编解码
uint8_t crc8(const uint8_t *data, size_t len);
bool decode_packet(const uint8_t *raw, size_t len, protocol_packet_t *out);
int encode_packet(uint8_t *out_buf, size_t out_size, uint8_t type,
                  const uint8_t *payload, size_t payload_len, uint8_t stream,
                  uint8_t epoch, uint32_t timestamp, uint32_t msg_id,
                  uint8_t part_idx, uint8_t total_parts, uint8_t sender);
int encode_ack(uint8_t *out_buf, size_t out_size, uint8_t acked_type,
               uint8_t acked_epoch);
int encode_mode_switch(uint8_t *out_buf, size_t out_size, uint8_t mode_byte);
int encode_history_req(uint8_t *out_buf, size_t out_size, uint32_t last_msg_id,
                       uint8_t direction);

int encode_data_packet(uint8_t *buf, const char *text);
#endif