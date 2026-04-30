#include "protocol.h"
#include "aes_crypto_helper.h" // 提供 aes_gcm_encrypt/decrypt, derive_session_key
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "mbedtls/gcm.h"
#include "nvs_helper.h"
#include <string.h>


static const char *TAG = "PROTOCOL";

/* ---------- 全局状态 ---------- */
device_mode_t g_current_mode = DEVICE_MODE_HOME;
static portMUX_TYPE g_mode_spinlock = portMUX_INITIALIZER_UNLOCKED;

/* 加密状态 */
static bool g_crypto_active = false;
static uint8_t g_session_key[16] = {0};

/* 用于解密载荷的静态缓冲区，一次只处理一帧（单线程安全） */
static uint8_t g_dec_buf[512];

/* ---------- 模式管理 ---------- */
void protocol_set_mode(device_mode_t mode) {
  portENTER_CRITICAL(&g_mode_spinlock);
  g_current_mode = mode;
  portEXIT_CRITICAL(&g_mode_spinlock);
  ESP_LOGI(TAG, "Mode switch to %d", mode);
}

device_mode_t protocol_get_mode(void) {
  device_mode_t mode;
  portENTER_CRITICAL(&g_mode_spinlock);
  mode = g_current_mode;
  portEXIT_CRITICAL(&g_mode_spinlock);
  return mode;
}

/* ---------- CRC ---------- */
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

/* ---------- 底层：明文帧构造 ---------- */
static int build_plain_packet(uint8_t *buf, size_t buf_size, uint8_t type,
                              uint8_t stream, const uint8_t *payload,
                              uint16_t payload_len) {
  const size_t total = 10 + payload_len + 1;
  if (buf_size < total)
    return -1;

  buf[0] = STX;
  buf[1] = type;
  buf[2] = stream;
  buf[3] = 0;
  uint32_t ts = 0;
  memcpy(&buf[4], &ts, 4);
  memcpy(&buf[8], &payload_len, 2);
  if (payload_len > 0)
    memcpy(&buf[10], payload, payload_len);
  buf[total - 1] = crc8(buf, total - 1);
  return total;
}

/* ---------- 底层：加密帧构造 ---------- */
static int build_encrypted_packet(uint8_t *buf, size_t buf_size, uint8_t type,
                                  uint8_t stream, const uint8_t *plain,
                                  size_t plain_len) {
  // 帧结构: 10字节头 + 12字节IV + 密文 + 16字节标签 + 1字节CRC
  const size_t required = 10 + 12 + plain_len + 16 + 1;
  if (buf_size < required)
    return -1;

  uint8_t iv[12];
  esp_fill_random(iv, sizeof(iv));
  uint8_t tag[16];
  uint8_t cipher[sizeof(g_dec_buf)]; // 与解密缓冲区一致大小

  if (plain_len > sizeof(cipher))
    return -1;

  // 构建 AAD：前10字节（STX, type, stream, epoch, timestamp, payload_len）
  uint8_t aad[10];
  aad[0] = STX;
  aad[1] = type;
  aad[2] = stream;
  aad[3] = 0;            // epoch
  memset(&aad[4], 0, 4); // timestamp
  uint16_t len16 = (uint16_t)plain_len;
  memcpy(&aad[8], &len16, 2);

  esp_err_t ret =
      aes_gcm_encrypt(g_session_key, 128, iv, sizeof(iv), aad, sizeof(aad),
                      plain, plain_len, cipher, tag, 16);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "GCM encrypt failed");
    return -1;
  }

  // 组装加密帧
  memcpy(buf, aad, 10);                  // 帧头
  memcpy(buf + 10, iv, 12);              // IV
  memcpy(buf + 22, cipher, plain_len);   // 密文
  memcpy(buf + 22 + plain_len, tag, 16); // 标签
  buf[required - 1] = crc8(buf, required - 1);
  return required;
}

/* ---------- 公开的 encode_packet ---------- */
int encode_packet(uint8_t *out_buf, size_t out_size, uint8_t type,
                  const uint8_t *payload, size_t payload_len, uint8_t stream,
                  uint8_t epoch, uint32_t timestamp, uint32_t msg_id,
                  uint8_t part_idx, uint8_t total_parts, uint8_t sender) {

  // 构建业务负载（对于 TYPE_DATA 需要内部 7 字节头）
  uint8_t send_buf[sizeof(g_dec_buf)];
  size_t send_len = payload_len;

  if (type == TYPE_DATA) {
    // 构造 DATA 头部（小端序）
    send_buf[0] = msg_id & 0xFF;
    send_buf[1] = (msg_id >> 8) & 0xFF;
    send_buf[2] = (msg_id >> 16) & 0xFF;
    send_buf[3] = (msg_id >> 24) & 0xFF;
    send_buf[4] = part_idx;
    send_buf[5] = total_parts;
    send_buf[6] = sender;
    if (payload_len > 0)
      memcpy(&send_buf[7], payload, payload_len);
    send_len = 7 + payload_len;
  } else if (payload_len > 0) {
    memcpy(send_buf, payload, payload_len);
  }

  // 决定是否加密（协商帧总是明文）
  if (g_crypto_active && type != TYPE_SESSION_RANDOM &&
      type != TYPE_SESSION_READY) {
    return build_encrypted_packet(out_buf, out_size, type, stream, send_buf,
                                  send_len);
  } else {
    return build_plain_packet(out_buf, out_size, type, stream, send_buf,
                              (uint16_t)send_len);
  }
}

/* ---------- 便捷函数 ---------- */
int encode_ack(uint8_t *out_buf, size_t out_size, uint8_t acked_type,
               uint8_t acked_epoch) {
  uint8_t payload[2] = {acked_type, acked_epoch};
  return encode_packet(out_buf, out_size, TYPE_ACK, payload, 2, STREAM_CONTROL,
                       acked_epoch, 0, 0, 0, 0, 0);
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
  return encode_packet(buf, 512, TYPE_DATA, (const uint8_t *)text, strlen(text),
                       STREAM_CHAT, 0, 0, 1, 0, 1, 1); // sender=1 (child)
}

/* ---------- 解密与解析 ---------- */
bool decode_packet(const uint8_t *raw, size_t len, protocol_packet_t *out) {
  if (!raw || !out || len < 11)
    return false;
  if (raw[0] != STX)
    return false;

  // CRC 校验
  if (crc8(raw, len - 1) != raw[len - 1]) {
    ESP_LOGW(TAG, "CRC mismatch");
    return false;
  }

  // 提取帧头
  out->type = raw[1];
  out->stream = raw[2];
  out->epoch = raw[3];
  memcpy(&out->timestamp, &raw[4], 4);

  const uint8_t *src_payload = NULL;
  uint16_t src_payload_len = 0;

  // 分支：加密帧 or 明文帧
  if (g_crypto_active && out->type != TYPE_SESSION_RANDOM &&
      out->type != TYPE_SESSION_READY) {
    // ------ 加密帧解密 ------
    if (len < 10 + 12 + 16 + 1)
      return false;
    uint16_t plain_len;
    memcpy(&plain_len, &raw[8], 2);
    size_t expected_len = 10 + 12 + plain_len + 16 + 1;
    if (len != expected_len)
      return false;

    const uint8_t *iv = raw + 10;
    const uint8_t *cipher = iv + 12;
    const uint8_t *tag = cipher + plain_len;

    uint8_t aad[10];
    memcpy(aad, raw, 10);

    esp_err_t ret =
        aes_gcm_decrypt(g_session_key, 128, iv, 12, aad, sizeof(aad), cipher,
                        plain_len, tag, 16, g_dec_buf);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Decrypt/auth failed");
      return false;
    }
    src_payload = g_dec_buf;
    src_payload_len = plain_len;
  } else {
    // ------ 明文帧 ------ */
    memcpy(&src_payload_len, &raw[8], 2);
    if (len < 10 + src_payload_len + 1)
      return false;
    src_payload = raw + 10;
  }

  // 填充通用字段
  out->payload_len = src_payload_len;
  out->payload = src_payload; // 指向内部静态缓冲区或原始帧数据

  // 根据类型提取业务字段
  if (out->type == TYPE_DATA) {
    if (src_payload_len < 7)
      return false;
    uint32_t mid;
    memcpy(&mid, src_payload, 4);
    out->msg_id = mid;
    out->part_idx = src_payload[4];
    out->total_parts = src_payload[5];
    out->sender = src_payload[6];
  } else if (out->type == TYPE_NOTIFY) {
    if (src_payload_len >= 5) {
      memcpy(&out->notify_msg_id, src_payload, 4);
      out->notify_sender = src_payload[4];
      size_t preview_sz = src_payload_len - 5;
      if (preview_sz > sizeof(out->notify_preview) - 1)
        preview_sz = sizeof(out->notify_preview) - 1;
      memcpy(out->notify_preview, &src_payload[5], preview_sz);
      out->notify_preview[preview_sz] = '\0';
    }
  } else if (out->type == TYPE_REASONING) {
    size_t text_len = src_payload_len;
    if (text_len > sizeof(out->reasoning_content) - 1)
      text_len = sizeof(out->reasoning_content) - 1;
    memcpy(out->reasoning_content, src_payload, text_len);
    out->reasoning_content[text_len] = '\0';
  }

  return true;
}

/* ---------- 加密会话管理 ---------- */
bool protocol_is_crypto_active(void) { return g_crypto_active; }

void protocol_activate_crypto(const uint8_t *session_key) {
  memcpy(g_session_key, session_key, 16);
  g_crypto_active = true;
  ESP_LOGI(TAG, "Encryption activated");
}

void protocol_reset_crypto(void) {
  memset(g_session_key, 0, 16);
  g_crypto_active = false;
}

bool protocol_derive_session_key(const uint8_t *server_random,
                                 size_t random_len, uint8_t *session_key_out) {
  if (random_len != 32)
    return false;

  uint8_t dev_key[16];
  if (nvs_helper_get_device_key(dev_key) != ESP_OK) {
    ESP_LOGE(TAG, "Device key not found in NVS");
    return false;
  }

  esp_err_t ret = derive_session_key(dev_key, 16, server_random, 32,
                                     (const uint8_t *)"guardian-session-v1", 20,
                                     session_key_out);
  return (ret == ESP_OK);
}