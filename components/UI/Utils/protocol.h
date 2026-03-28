#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define STX 0x02

#define TYPE_MODE_SWITCH 0x00
#define TYPE_CMD 0x01
#define TYPE_DATA 0x02
#define TYPE_REASONING 0x03
#define TYPE_ACK 0x05
#define TYPE_HISTORY_REQ 0x06
#define TYPE_SYN 0x07
#define TYPE_END 0x08
#define TYPE_NOTIFY 0x09

#define STREAM_CONTROL 0x00
#define STREAM_CHAT 0x01
#define STREAM_REASONING 0x02

typedef struct {
  uint8_t type;
  uint8_t stream;
  uint8_t epoch;
  uint32_t timestamp;

  uint32_t msg_id;
  uint8_t sender;
  char content[256];

  uint32_t notify_msg_id;
} protocol_packet_t;

uint8_t crc8(const uint8_t *data, int len);

bool decode_packet(const uint8_t *buf, int len, protocol_packet_t *out);

int encode_mode_switch_packet(uint8_t *buf, uint8_t mode);

int encode_history_req(uint8_t *buf, uint32_t last_id, uint8_t direction);

int encode_data_packet(uint8_t *buf, const char *text);

int encode_ack_packet(uint8_t *buf, uint8_t ack_type, uint8_t epoch);

#endif
