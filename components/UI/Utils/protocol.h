#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/* ---------- 常量 ---------- */
#define STX 0x02
#define CRC8_POLY 0x31

/* 帧类型 */
#define TYPE_MODE_SWITCH 0x00
#define TYPE_CMD 0x01
#define TYPE_DATA 0x02
#define TYPE_REASONING 0x03
#define TYPE_ACK 0x05
#define TYPE_HISTORY_REQ 0x06
#define TYPE_SYN 0x07
#define TYPE_END 0x08
#define TYPE_NOTIFY 0x09
#define TYPE_SESSION_RANDOM 0x0A // 服务端→设备，32字节随机数
#define TYPE_SESSION_READY 0x0B  // 设备→服务端，空payload，表示加密就绪

/* 流标识 */
#define STREAM_CONTROL 0x00
#define STREAM_CHAT 0x01
#define STREAM_REASONING 0x02

/* 历史方向 */
#define HISTORY_DIR_OLDER 0x00
#define HISTORY_DIR_NEWER 0x01

/* 单条消息最大字节数（UTF-8）— 对齐 msg_t.text[256] / reasoning_content[256]
 * 缓冲区，与服务端 MAX_MESSAGE_BYTES 同值 */
#define MAX_MESSAGE_BYTES 254

/* 设备模式 */
typedef enum {
  DEVICE_MODE_HOME,
  DEVICE_MODE_CHAT_LIVE,
  DEVICE_MODE_CHAT_HISTORY
} device_mode_t;

/* 解析后的数据包（兼容旧字段） */
typedef struct {
  uint8_t type;
  uint8_t stream;
  uint8_t epoch;
  uint32_t timestamp;
  uint16_t payload_len;   // 有效负载长度（字节）
  const uint8_t *payload; // 指向内部缓冲区，仅在下次调用 decode_packet 前有效

  // TYPE_DATA 专用字段
  uint32_t msg_id;
  uint8_t part_idx;
  uint8_t total_parts;
  uint8_t sender; // 1=child, 0=parent

  // TYPE_NOTIFY 专用
  uint32_t notify_msg_id;
  uint8_t notify_sender;
  char notify_preview[64];

  // TYPE_REASONING 专用
  char reasoning_content[256];
} protocol_packet_t;

/* ---------- 模式管理 ---------- */
void protocol_set_mode(device_mode_t mode);
device_mode_t protocol_get_mode(void);

/* ---------- CRC ---------- */
uint8_t crc8(const uint8_t *data, size_t len);

/* ---------- 编解码（透明加解密） ---------- */
bool decode_packet(const uint8_t *raw, size_t len, protocol_packet_t *out);

int encode_packet(uint8_t *out_buf, size_t out_size, uint8_t type,
                  const uint8_t *payload, size_t payload_len, uint8_t stream,
                  uint8_t epoch, uint32_t timestamp, uint32_t msg_id,
                  uint8_t part_idx, uint8_t total_parts, uint8_t sender);

/* 便捷发送函数 */
int encode_ack(uint8_t *out_buf, size_t out_size, uint8_t acked_type,
               uint8_t acked_epoch);
int encode_mode_switch(uint8_t *out_buf, size_t out_size, uint8_t mode_byte);
int encode_history_req(uint8_t *out_buf, size_t out_size, uint32_t last_msg_id,
                       uint8_t direction);
int encode_data_packet(uint8_t *buf, const char *text);

/* ---------- 加密会话管理 ---------- */
bool protocol_is_crypto_active(void);
void protocol_activate_crypto(const uint8_t *session_key); // 传入16字节会话密钥
void protocol_reset_crypto(void);                          // 断开时重置

/** 由服务端随机数派生会话密钥，成功返回 true */
bool protocol_derive_session_key(const uint8_t *server_random,
                                 size_t random_len, uint8_t *session_key_out);

#endif