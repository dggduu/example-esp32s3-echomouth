// /*
//  * ULP RISC-V program — one-shot PCA9539 config + GPIO4 wake.
//  *
//  * Runs ONCE when deep sleep starts:
//  *   1. Configure PCA9539 via RTC I2C (screen ON, backlight OFF, others OFF)
//  *   2. Exit → main CPU enters deep sleep
//  *
//  * Wake source: ULP_RISCV_WAKEUP_SOURCE_GPIO (GPIO4 = TP_INT)
//  *   CST816S touch → GPIO4 LOW → hardware wakes main CPU.
//  *
//  * RTC I2C pins (ESP32-S3 fixed):
//  *   SCL = GPIO2, SDA = GPIO3
//  */

// #include "ulp_riscv.h"
// #include "ulp_riscv_gpio.h"
// /* ULP core I2C: no init needed — main CPU calls ulp_riscv_i2c_master_init()
// before sleep */

// #define PCA9539_ADDR      0x74
// #define PCA_REG_CONFIG0   0x03
// #define PCA_REG_CONFIG1   0x07
// #define PCA_REG_OUTPUT0   0x01
// #define PCA_REG_OUTPUT1   0x05

// /* PORT0 = 0x00: PA off, audio off, cam off
//    PORT1 = 0x10: bit4=HIGH backlight off (PMOS), bit5=LOW screen on (PMOS) */
// #define PORT0_VAL 0x00
// #define PORT1_VAL 0x10

// #define WAKE_GPIO GPIO_NUM_4

// volatile uint32_t ulp_wake_by_gpio = 0;

// int main(void)
// {
//     /* RTC I2C is already initialized by the main CPU.
//        Just configure PCA9539 registers. */

//     ulp_riscv_i2c_master_set_slave_addr(PCA9539_ADDR);
//     uint8_t d = 0x00;

//     /* config port0 → all outputs */
//     ulp_riscv_i2c_master_set_slave_reg_addr(PCA_REG_CONFIG0);
//     ulp_riscv_i2c_master_write_to_device(&d, 1);

//     /* config port1 → all outputs */
//     ulp_riscv_i2c_master_set_slave_reg_addr(PCA_REG_CONFIG1);
//     ulp_riscv_i2c_master_write_to_device(&d, 1);

//     /* output port0: all off */
//     d = PORT0_VAL;
//     ulp_riscv_i2c_master_set_slave_reg_addr(PCA_REG_OUTPUT0);
//     ulp_riscv_i2c_master_write_to_device(&d, 1);

//     /* output port1: screen on, backlight off */
//     d = PORT1_VAL;
//     ulp_riscv_i2c_master_set_slave_reg_addr(PCA_REG_OUTPUT1);
//     ulp_riscv_i2c_master_write_to_device(&d, 1);

//     /* configure wake GPIO */
//     ulp_riscv_gpio_init(WAKE_GPIO);
//     ulp_riscv_gpio_input_enable(WAKE_GPIO);
//     ulp_riscv_gpio_pullup(WAKE_GPIO);

//     ulp_wake_by_gpio = 1;
//     return 0;
// }
/*
 * ULP RISC-V program — interrupt-mode wake via TP_INT (GPIO4).
 *
 * Runs periodically (20 ms) while the main CPU is in deep sleep.
 * CST816S touch controller pulls GPIO4 LOW on touch → ULP wakes CPU
 * immediately. Minimal debounce: 2 consecutive samples (~40 ms).
 *
 * PCA9539 must be configured BEFORE deep sleep:
 *   Screen power ON  → touch panel stays powered
 *   Backlight OFF     → save power
 */

#include "ulp_riscv.h"
#include "ulp_riscv_gpio.h"
#include "ulp_riscv_utils.h"

#define WAKE_GPIO GPIO_NUM_4
#define DEBOUNCE_SAMPLES 2

volatile uint32_t ulp_wake_by_gpio = 0;

int main(void) {
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