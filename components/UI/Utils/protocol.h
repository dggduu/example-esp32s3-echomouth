#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

/* 协议常量 */
#define STX 0x02

/* 包类型 */
#define TYPE_MODE_SWITCH 0x00
#define TYPE_CMD 0x01
#define TYPE_DATA 0x02
#define TYPE_REASONING 0x03
#define TYPE_ACK 0x05
#define TYPE_HISTORY_REQ 0x06
#define TYPE_SYN 0x07
#define TYPE_END 0x08
#define TYPE_NOTIFY 0x09
#define EMPTY_FILL 0x5a

/* 流类型 */
#define STREAM_CONTROL 0x00
#define STREAM_CHAT 0x01
#define STREAM_REASONING 0x02

/* 历史请求方向 */
#define HISTORY_DIR_OLDER 0x00
#define HISTORY_DIR_NEWER 0x01

/* 设备模式枚举 */
typedef enum {
  DEVICE_MODE_HOME,
  DEVICE_MODE_CHAT_LIVE,
  DEVICE_MODE_CHAT_HISTORY
} device_mode_t;

/* 解析后的数据包结构体 */
typedef struct {
  uint8_t type;
  uint8_t stream;
  uint8_t epoch;
  uint32_t timestamp;
  uint16_t payload_len;
  const uint8_t *payload;

  /* 以下为特定类型的解析字段 */
  uint32_t msg_id; /* TYPE_DATA */
  uint8_t part_idx;
  uint8_t total_parts;
  uint8_t sender;    /* 0: parent, 1: child */
  char content[256]; /* 消息内容（最大255字节） */

  uint32_t notify_msg_id;  /* TYPE_NOTIFY */
  uint8_t notify_sender;   /* TYPE_NOTIFY 的发送者 */
  char notify_preview[64]; /* TYPE_NOTIFY 的预览文本 */

  uint8_t reasoning_part_idx; /* TYPE_REASONING */
  uint8_t reasoning_total;
  char reasoning_content[256]; /* 推理消息内容 */

  uint8_t ack_type;  /* TYPE_ACK 确认的包类型 */
  uint8_t ack_epoch; /* TYPE_ACK 确认的 epoch */

  uint8_t mode_switch_value; /* TYPE_MODE_SWITCH */
  uint8_t cmd_value;         /* TYPE_CMD */
} protocol_packet_t;

/* 全局连接管理 */
void protocol_set_send_callback(int (*send_func)(const uint8_t *data, int len));
void protocol_on_connected(void);
void protocol_on_disconnected(void);

/* 模式管理 */
device_mode_t protocol_get_mode(void);
void protocol_switch_mode(device_mode_t new_mode);
bool protocol_send_mode_switch(device_mode_t mode);

/* 聊天功能 */
bool protocol_send_message(const char *text);
bool protocol_request_history(uint32_t last_msg_id, uint8_t direction);

/* 低层封包/解包 */
bool decode_packet(const uint8_t *buf, int len, protocol_packet_t *out);
int encode_ack_packet(uint8_t *buf, uint8_t ack_type, uint8_t epoch);

/* 用户需实现此回调：当收到完整数据包时调用 */
void protocol_on_packet_received(const protocol_packet_t *pkt);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */