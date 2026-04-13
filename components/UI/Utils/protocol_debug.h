#ifndef PROTOCOL_DEBUG_H
#define PROTOCOL_DEBUG_H

#include "protocol.h"
#include <stddef.h>
#include <stdint.h>

// 打印解码后的协议包内容
void print_protocol_packet(const protocol_packet_t *pkt);

// 打印原始数据包（十六进制 + ASCII）
void print_raw_packet(const uint8_t *data, size_t len, const char *prefix);

// 获取类型名称字符串
const char *packet_type_to_str(uint8_t type);

// 获取流名称字符串
const char *stream_to_str(uint8_t stream);

#endif