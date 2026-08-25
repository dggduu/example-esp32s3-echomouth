#ifndef __POWER_MANAGER_H__
#define __POWER_MANAGER_H__

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t power_manager_init(void);
void power_manager_report_activity(void);
void power_manager_enter_sleep(void);
void power_manager_exit_sleep(void);
bool power_manager_is_screen_off(void);
bool power_manager_is_sleeping(void);

/**
 * @brief Load ULP RISC-V program and enter deep sleep.
 *        ULP monitors GPIO0 and wakes the main CPU on button press.
 *        On wake, the system reboots from app_main.
 */
void power_manager_enter_deep_sleep(void);

/**
 * @brief Check whether the system just woke from ULP deep sleep.
 *        Call this early in app_main to decide resume vs fresh boot.
 */
bool power_manager_is_deep_sleep_wakeup(void);

#ifdef __cplusplus
}
#endif

#endif