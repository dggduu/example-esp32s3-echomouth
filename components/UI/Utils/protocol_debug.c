#include "protocol_debug.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "PROTOCOL_DEBUG";

const char *packet_type_to_str(uint8_t type) {
  switch (type) {
  case TYPE_MODE_SWITCH:
    return "MODE_SWITCH";
  case TYPE_CMD:
    return "CMD";
  case TYPE_DATA:
    return "DATA";
  case TYPE_REASONING:
    return "REASONING";
  case TYPE_ACK:
    return "ACK";
  case TYPE_HISTORY_REQ:
    return "HISTORY_REQ";
  case TYPE_SYN:
    return "SYN";
  case TYPE_END:
    return "END";
  case TYPE_NOTIFY:
    return "NOTIFY";
  default:
    return "UNKNOWN";
  }
}

const char *stream_to_str(uint8_t stream) {
  switch (stream) {
  case STREAM_CONTROL:
    return "CONTROL";
  case STREAM_CHAT:
    return "CHAT";
  case STREAM_REASONING:
    return "REASONING";
  default:
    return "UNKNOWN";
  }
}

void print_protocol_packet(const protocol_packet_t *pkt) {
  if (!pkt) {
    ESP_LOGE(TAG, "print_protocol_packet: null packet");
    return;
  }

  printf("\n========== Protocol Packet ==========\n");
  printf("  type       : 0x%02X (%s)\n", pkt->type,
         packet_type_to_str(pkt->type));
  printf("  stream     : 0x%02X (%s)\n", pkt->stream,
         stream_to_str(pkt->stream));
  printf("  epoch      : %u\n", pkt->epoch);
  printf("  timestamp  : %lu\n", (unsigned long)pkt->timestamp);
  printf("  payload_len: %u\n", pkt->payload_len);

  if (pkt->type == TYPE_DATA) {
    printf("  [DATA] msg_id     : %lu\n", (unsigned long)pkt->msg_id);
    printf("  [DATA] total_parts: %u\n", pkt->total_parts);
    printf("  [DATA] part_idx   : %u\n", pkt->part_idx);
    printf("  [DATA] sender     : %s\n", pkt->sender == 1 ? "child" : "parent");

    size_t content_len = pkt->payload_len - 7;
    if (content_len > 0 && pkt->payload) {
      printf("  [DATA] content    : ");
      fwrite(pkt->payload + 7, 1, content_len, stdout);
      printf("\n");
    } else {
      printf("  [DATA] content    : (empty)\n");
    }
  } else if (pkt->type == TYPE_NOTIFY) {
    printf("  [NOTIFY] msg_id   : %lu\n", (unsigned long)pkt->notify_msg_id);
    printf("  [NOTIFY] sender   : %s\n",
           pkt->notify_sender == 1 ? "child" : "parent");
    printf("  [NOTIFY] preview  : %s\n", pkt->notify_preview);
  } else if (pkt->type == TYPE_REASONING) {
    printf("  [REASONING] content: %s\n", pkt->reasoning_content);
  } else {
    if (pkt->payload_len > 0 && pkt->payload) {
      printf("  payload text : ");
      fwrite(pkt->payload, 1, pkt->payload_len, stdout);
      printf("\n");
    } else {
      printf("  payload text : (empty)\n");
    }
  }

  // 打印 payload 原始十六进制（前 32 字节）
  size_t hex_len = pkt->payload_len;
  if (hex_len > 32)
    hex_len = 32;
  if (hex_len > 0) {
    printf("  payload hex  : ");
    for (size_t i = 0; i < hex_len; i++) {
      printf("%02X ", pkt->payload[i]);
    }
    if (pkt->payload_len > 32)
      printf("...");
    printf("\n");
  }
  printf("=====================================\n");
}

void print_raw_packet(const uint8_t *data, size_t len, const char *prefix) {
  if (!data || len == 0) {
    ESP_LOGW(TAG, "print_raw_packet: empty data");
    return;
  }

  if (prefix) {
    printf("%s", prefix);
  } else {
    printf("RAW PACKET (%zu bytes): ", len);
  }

  // 打印十六进制
  for (size_t i = 0; i < len; i++) {
    printf("%02X ", data[i]);
    if ((i + 1) % 16 == 0 && i + 1 < len) {
      printf("\n");
      if (prefix)
        printf("%*s", (int)strlen(prefix), "");
    }
  }

  // 打印 ASCII 可读部分
  printf("\n  ASCII: ");
  for (size_t i = 0; i < len; i++) {
    uint8_t c = data[i];
    if (c >= 0x20 && c < 0x7F) {
      printf("%c", c);
    } else {
      printf(".");
    }
  }
  printf("\n");
}