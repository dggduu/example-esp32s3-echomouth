/*
 * ULP RISC-V 触摸唤醒程序 — 软件模拟 I2C 轮询 CST816S (SDA=GPIO6, SCL=GPIO5)
 */
#include "ulp_riscv.h"
#include "ulp_riscv_gpio.h"
#include "ulp_riscv_utils.h"

#define SDA_PIN GPIO_NUM_6
#define SCL_PIN GPIO_NUM_5

#define CST816S_ADDR 0x15
#define CST816S_REG_FINGER_NUM 0x02
#define DEBOUNCE_SAMPLES 2

volatile uint32_t ulp_wake_by_gpio = 0;

/* --- 软件模拟 I2C 底层实现 --- */

// 约 100kHz 时钟延时
static inline void i2c_delay(void) { ulp_riscv_delay_cycles(20); }

static inline void i2c_sda_high(void) {
  ulp_riscv_gpio_output_level(SDA_PIN, 1);
}

static inline void i2c_sda_low(void) {
  ulp_riscv_gpio_output_level(SDA_PIN, 0);
}

static inline void i2c_scl_high(void) {
  ulp_riscv_gpio_output_level(SCL_PIN, 1);
}

static inline void i2c_scl_low(void) {
  ulp_riscv_gpio_output_level(SCL_PIN, 0);
}

static void i2c_start(void) {
  i2c_sda_high();
  i2c_scl_high();
  i2c_delay();
  i2c_sda_low();
  i2c_delay();
  i2c_scl_low();
  i2c_delay();
}

static void i2c_stop(void) {
  i2c_sda_low();
  i2c_delay();
  i2c_scl_high();
  i2c_delay();
  i2c_sda_high();
  i2c_delay();
}

static uint8_t i2c_write_byte(uint8_t data) {
  for (int i = 0; i < 8; i++) {
    if (data & 0x80) {
      i2c_sda_high();
    } else {
      i2c_sda_low();
    }
    data <<= 1;
    i2c_delay();
    i2c_scl_high();
    i2c_delay();
    i2c_scl_low();
  }

  // 释放 SDA 读取 ACK
  i2c_sda_high();
  i2c_delay();
  i2c_scl_high();
  i2c_delay();
  uint8_t ack = ulp_riscv_gpio_get_level(SDA_PIN);
  i2c_scl_low();

  return ack;
}

static uint8_t i2c_read_byte(uint8_t ack) {
  uint8_t data = 0;
  i2c_sda_high(); // 释放 SDA 进入输入模式

  for (int i = 0; i < 8; i++) {
    data <<= 1;
    i2c_scl_high();
    i2c_delay();
    if (ulp_riscv_gpio_get_level(SDA_PIN)) {
      data |= 1;
    }
    i2c_scl_low();
    i2c_delay();
  }

  // 发送 ACK (1) 或 NACK (0)
  if (ack) {
    i2c_sda_low();
  } else {
    i2c_sda_high();
  }
  i2c_delay();
  i2c_scl_high();
  i2c_delay();
  i2c_scl_low();

  return data;
}

// 读取 CST816S 寄存器
static bool cst816s_read_reg(uint8_t reg, uint8_t *val) {
  // 1. 写寄存器地址
  i2c_start();
  if (i2c_write_byte(CST816S_ADDR << 1) != 0) { // NACK
    i2c_stop();
    return false;
  }
  if (i2c_write_byte(reg) != 0) { // NACK
    i2c_stop();
    return false;
  }

  // 2. Restart 并读取数据
  i2c_start();
  if (i2c_write_byte((CST816S_ADDR << 1) | 1) != 0) { // NACK
    i2c_stop();
    return false;
  }

  *val = i2c_read_byte(0); // 单字节读取，发送 NACK
  i2c_stop();
  return true;
}

/* --- ULP 主逻辑 --- */

int main(void) {
  static uint32_t touch_samples = 0;
  uint8_t finger_count = 0;

  if (cst816s_read_reg(CST816S_REG_FINGER_NUM, &finger_count) &&
      finger_count > 0) {
    touch_samples++;
    if (touch_samples >= DEBOUNCE_SAMPLES) {
      ulp_wake_by_gpio = 1;
      ulp_riscv_wakeup_main_processor();
      touch_samples = 0;
    }
  } else {
    touch_samples = 0;
  }

  return 0;
}