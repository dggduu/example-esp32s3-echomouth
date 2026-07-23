/*
 * ULP RISC-V program — interrupt-mode wake via TP_INT (GPIO4).
 *
 * Runs periodically (20 ms) while the main CPU is in deep sleep.
 * CST816S touch controller pulls GPIO4 LOW on touch → ULP wakes CPU immediately.
 * Minimal debounce: 2 consecutive samples (~40 ms).
 *
 * PCA9539 must be configured BEFORE deep sleep:
 *   Screen power ON  → touch panel stays powered
 *   Backlight OFF     → save power
 */

#include "ulp_riscv.h"
#include "ulp_riscv_gpio.h"
#include "ulp_riscv_utils.h"

#define WAKE_GPIO        GPIO_NUM_4
#define DEBOUNCE_SAMPLES 2

volatile uint32_t ulp_wake_by_gpio = 0;

int main(void)
{
    /* Configure RTC GPIO4 as input with internal pull-up.
     * CST816S TP_INT is open-drain: idle=HIGH (pulled up), touch=LOW. */
    ulp_riscv_gpio_init(WAKE_GPIO);
    ulp_riscv_gpio_input_enable(WAKE_GPIO);
    ulp_riscv_gpio_pullup(WAKE_GPIO);

    static uint32_t sample_count = 0;

    if (ulp_riscv_gpio_get_level(WAKE_GPIO) == 0) {
        sample_count++;
        if (sample_count >= DEBOUNCE_SAMPLES) {
            ulp_wake_by_gpio = 1;
            ulp_riscv_wakeup_main_processor();
            sample_count = 0;
        }
    } else {
        sample_count = 0;
    }

    return 0;
}
