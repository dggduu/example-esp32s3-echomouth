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

#ifdef __cplusplus
}
#endif

#endif /* AES_CRYPTO_HELPER_H */