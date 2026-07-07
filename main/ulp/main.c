/*
 * ULP RISC-V program — GPIO0 monitor for wake-up from deep sleep.
 *
 * Runs periodically (e.g. every 20 ms) while the main CPU is in deep sleep.
 * Monitors GPIO0 (active-low button).  When GPIO0 is pressed (LOW) for a
 * configurable number of consecutive samples, it wakes the main CPU.
 */

#include "ulp_riscv.h"
#include "ulp_riscv_gpio.h"
#include "ulp_riscv_utils.h"

/* Number of consecutive LOW samples before wake-up (debounce).
   With a 20 ms period this gives ~100 ms debounce time. */
#define DEBOUNCE_COUNT 5

/* Exported variables — accessible from the main CPU via ulp_main.h */
volatile uint32_t ulp_wake_by_gpio = 0;

int main(void)
{
    /* Configure RTC GPIO0 as input with internal pull-up */
    ulp_riscv_gpio_init(GPIO_NUM_0);
    ulp_riscv_gpio_input_enable(GPIO_NUM_0);
    ulp_riscv_gpio_pullup(GPIO_NUM_0);

    /* Debounce counter (lives in RTC memory across ULP wake cycles) */
    static uint32_t debounce = 0;

    if (ulp_riscv_gpio_get_level(GPIO_NUM_0) == 0) {
        debounce++;
        if (debounce >= DEBOUNCE_COUNT) {
            ulp_wake_by_gpio = 1;
            ulp_riscv_wakeup_main_processor();
            debounce = 0;
        }
    } else {
        debounce = 0;
    }

    return 0;
}
