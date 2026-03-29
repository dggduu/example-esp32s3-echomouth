#ifndef PROV_QR_H
#define PROV_QR_H

#include "gs_qrcode_comp.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 显示二维码页面
 * @param name      设备名
 * @param username  用户名
 * @param pop       个人识别码
 * @param transport 传输方式
 */
void wifi_prov_print_qr(const char *name, const char *username, const char *pop,
                        const char *transport);

/**
 * @brief 更新二维码页面状态
 * @param status 状态（等待、成功、失败）
 */
void prov_qr_set_status(gs_qr_status_t status);

/**
 * @brief 初始化二维码显示模块
 */
void prov_qr_init(void);

/**
 * @brief 在 LVGL 任务循环中调用，处理二维码显示请求
 */
void prov_qr_process(void);

#ifdef __cplusplus
}
#endif

#endif // PROV_QR_H