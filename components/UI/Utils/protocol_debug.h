#ifndef PROTOCOL_DEBUG_H
#define PROTOCOL_DEBUG_H

#include "protocol.h"
#include <stddef.h>
#include <stdint.h>

void print_protocol_packet(const protocol_packet_t *pkt);

void print_raw_packet(const uint8_t *data, size_t len, const char *prefix);

const char *packet_type_to_str(uint8_t type);

const char *stream_to_str(uint8_t stream);

#endif