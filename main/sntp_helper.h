#ifndef sntp_helper_H
#define sntp_helper_H

#include "esp_err.h"
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 NTP 同步组件
 * @return ESP_OK 成功，其他失败
 */
esp_err_t sntp_helper_init(void);

/**
 * @brief 设置 NTP 服务器并启动同步
 * @param server NTP 服务器域名或 IP
 * @param timeout_ms 最大等待时间（毫秒），若为 0 则无限等待
 * @return ESP_OK 成功，ESP_ERR_TIMEOUT 超时，其他失败
 */
esp_err_t sntp_helper_time(const char *server, int timeout_ms);

/**
 * @brief 设置本地时区
 * @param tz_string 时区字符串（如 "CST-8"）
 * @return ESP_OK 成功，ESP_ERR_INVALID_ARG 参数无效
 */
esp_err_t sntp_helper_set_timezone(const char *tz_string);

/**
 * @brief 获取最后一次成功同步的 Unix 时间戳（秒）
 * @return Unix 时间戳，若未同步过则返回 0
 */
time_t sntp_helper_get_last_timestamp(void);

/**
 * @brief 获取当前 Unix 时间戳（毫秒）
 * @return 当前毫秒时间戳，若系统时间未同步或出错返回 0
 */
uint64_t sntp_helper_get_timestamp_ms(void);

/**
 * @brief 释放 NTP 资源
 */
void sntp_helper_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* sntp_helper_H */