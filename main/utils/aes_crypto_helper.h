#ifndef AES_CRYPTO_HELPER_H
#define AES_CRYPTO_HELPER_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册使用AES加密模块（增加引用计数）
 * @return ESP_OK 成功，ESP_ERR_NO_MEM 等错误码
 */
esp_err_t aes_crypto_register(void);

/**
 * @brief 注销使用AES加密模块（减少引用计数，计数为0时释放硬件资源）
 * @return ESP_OK 成功
 */
esp_err_t aes_crypto_unregister(void);

/**
 * @brief AES-CBC加密缓冲区（CBC模式，要求输入长度是16的倍数）
 * @param key     密钥缓冲区
 * @param key_bits 密钥位数（128/192/256）
 * @param iv      初始化向量（16字节），会被修改（输出密文后IV更新为下一块IV）
 * @param input   明文缓冲区
 * @param in_len  明文长度（字节），必须是16的倍数
 * @param output  密文缓冲区（至少in_len字节）
 * @param out_len 实际输出长度（等于in_len）
 * @return ESP_OK 成功，其他失败
 */
esp_err_t aes_cbc_encrypt(const uint8_t *key, size_t key_bits, uint8_t *iv,
                          const uint8_t *input, size_t in_len, uint8_t *output,
                          size_t *out_len);

/**
 * @brief AES-CBC解密缓冲区
 * @param key     密钥缓冲区
 * @param key_bits 密钥位数
 * @param iv      初始化向量（16字节），会被修改
 * @param input   密文缓冲区
 * @param in_len  密文长度（16的倍数）
 * @param output  明文缓冲区
 * @param out_len 实际输出长度
 * @return ESP_OK 成功
 */
esp_err_t aes_cbc_decrypt(const uint8_t *key, size_t key_bits, uint8_t *iv,
                          const uint8_t *input, size_t in_len, uint8_t *output,
                          size_t *out_len);

/**
 * @brief AES-ECB加密一个块（16字节）
 * @param key       密钥缓冲区
 * @param key_bits  密钥位数
 * @param input     16字节明文
 * @param output    16字节密文
 * @return ESP_OK 成功
 */
esp_err_t aes_ecb_encrypt_block(const uint8_t *key, size_t key_bits,
                                const uint8_t *input, uint8_t *output);

/**
 * @brief AES-ECB解密一个块（16字节）
 * @param key       密钥缓冲区
 * @param key_bits  密钥位数
 * @param input     16字节密文
 * @param output    16字节明文
 * @return ESP_OK 成功
 */
esp_err_t aes_ecb_decrypt_block(const uint8_t *key, size_t key_bits,
                                const uint8_t *input, uint8_t *output);

/**
 * @brief 加密文件（将输入文件加密后写入输出文件，使用CBC模式+PKCS#7填充）
 * @param in_path  源文件路径
 * @param out_path 目标文件路径
 * @param key      密钥
 * @param key_bits 密钥位数
 * @return ESP_OK 成功
 * @note 加密文件格式：[16字节随机IV] + [PKCS#7填充后的密文]
 */
esp_err_t aes_encrypt_file(const char *in_path, const char *out_path,
                           const uint8_t *key, size_t key_bits);

/**
 * @brief 解密文件（加密文件的反向操作）
 * @param in_path  加密文件路径
 * @param out_path 解密输出文件路径
 * @param key      密钥
 * @param key_bits 密钥位数
 * @return ESP_OK 成功
 */
esp_err_t aes_decrypt_file(const char *in_path, const char *out_path,
                           const uint8_t *key, size_t key_bits);

/**
 * @brief AES‑GCM 加密
 * @param key        128/192/256 位密钥
 * @param key_bits   密钥位数
 * @param iv        初始化向量（推荐 12 字节，也可 1~16 字节）
 * @param iv_len    IV 长度（字节）
 * @param aad       附加认证数据（可为 NULL）
 * @param aad_len   AAD 长度
 * @param plain     明文数据
 * @param plain_len 明文长度
 * @param cipher    输出密文缓冲区（至少 plain_len 字节）
 * @param tag       输出认证标签（推荐 16 字节）
 * @param tag_len   标签长度（如 16）
 * @return ESP_OK 成功
 */
esp_err_t aes_gcm_encrypt(const uint8_t *key, size_t key_bits,
                          const uint8_t *iv, size_t iv_len, const uint8_t *aad,
                          size_t aad_len, const uint8_t *plain,
                          size_t plain_len, uint8_t *cipher, uint8_t *tag,
                          size_t tag_len);

/**
 * @brief AES‑GCM 解密
 * @param key        密钥
 * @param key_bits   密钥位数
 * @param iv        初始化向量
 * @param iv_len    IV 长度
 * @param aad       附加认证数据
 * @param aad_len   AAD 长度
 * @param cipher    密文数据
 * @param cipher_len 密文长度
 * @param tag       认证标签
 * @param tag_len   标签长度
 * @param plain     输出明文缓冲区
 * @return ESP_OK 成功（且认证通过），ESP_FAIL 认证失败
 */
esp_err_t aes_gcm_decrypt(const uint8_t *key, size_t key_bits,
                          const uint8_t *iv, size_t iv_len, const uint8_t *aad,
                          size_t aad_len, const uint8_t *cipher,
                          size_t cipher_len, const uint8_t *tag, size_t tag_len,
                          uint8_t *plain);

/**
 * @brief 由设备根密钥（eFuse UUID）派生会话密钥
 * @param root_key      根密钥（128/256 位，如 UUID）
 * @param root_key_len  根密钥长度（字节）
 * @param salt          盐值（可为 NULL）
 * @param salt_len      盐值长度
 * @param info          上下文信息（如 "guardian-session"）
 * @param info_len      info 长度
 * @param session_key   输出 128 位会话密钥（16 字节）
 * @return ESP_OK 成功
 */
esp_err_t derive_session_key(const uint8_t *root_key, size_t root_key_len,
                             const uint8_t *salt, size_t salt_len,
                             const uint8_t *info, size_t info_len,
                             uint8_t *session_key);

#ifdef __cplusplus
}
#endif

#endif /* AES_CRYPTO_HELPER_H */