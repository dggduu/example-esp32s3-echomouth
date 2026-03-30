#ifndef PROV_QR_H
#define PROV_QR_H

#include "gs_qrcode_comp.h"

// 初始化 UI 队列及资源
void prov_qr_init(void);

// LVGL 任务循环调用，处理 UI 刷新
void prov_qr_process(void);

// 供配网任务调用：推送二维码数据
void wifi_prov_print_qr(const char *name, const char *username, const char *pop,
                        const char *transport);

// 供配网任务调用：更新 UI 状态
void prov_qr_set_status(gs_qr_status_t status);

// 供配网任务调用：彻底销毁页面
void prov_qr_close();

#endif