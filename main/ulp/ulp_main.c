#include "ulp_riscv.h"
#include "ulp_riscv_gpio.h"
#include "ulp_riscv_utils.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// 引脚映射定义
#define ULP_I2C_SDA_PIN GPIO_NUM_6
#define ULP_I2C_SCL_PIN GPIO_NUM_5

#define CST816S_ADDR 0x15
#define REG_FINGER_NUM 0x02

// =================【ULP 软件 I2C 底层实现】=================
static inline void ulp_i2c_delay(void) {
  // ULP 运行频率通常较低，5us ~ 10us 延迟是安全稳健的
  ulp_riscv_delay_us(10);
}

static inline void set_sda(uint8_t level) {
  ulp_riscv_gpio_output_level(ULP_I2C_SDA_PIN, level);
}

static inline void set_scl(uint8_t level) {
  ulp_riscv_gpio_output_level(ULP_I2C_SCL_PIN, level);
}

static inline uint8_t get_sda(void) {
  return ulp_riscv_gpio_get_level(ULP_I2C_SDA_PIN);
}

static void soft_i2c_start(void) {
  set_sda(1);
  set_scl(1);
  ulp_i2c_delay();
  set_sda(0);
  ulp_i2c_delay();
  set_scl(0);
  ulp_i2c_delay();
}

static void soft_i2c_stop(void) {
  set_sda(0);
  set_scl(0);
  ulp_i2c_delay();
  set_scl(1);
  ulp_i2c_delay();
  set_sda(1);
  ulp_i2c_delay();
}

static bool soft_i2c_write_byte(uint8_t data) {
  for (int i = 7; i >= 0; i--) {
    set_sda((data >> i) & 0x01);
    ulp_i2c_delay();
    set_scl(1);
    ulp_i2c_delay();
    set_scl(0);
    ulp_i2c_delay();
  }
  // 释放 SDA 以接收 ACK
  set_sda(1);
  ulp_i2c_delay();
  set_scl(1);
  ulp_i2c_delay();
  uint8_t ack = get_sda();
  set_scl(0);
  ulp_i2c_delay();
  return (ack == 0);
}

static uint8_t soft_i2c_read_byte(bool ack) {
  uint8_t data = 0;
  set_sda(1); // 输入状态准备
  for (int i = 7; i >= 0; i--) {
    set_scl(1);
    ulp_i2c_delay();
    if (get_sda()) {
      data |= (1 << i);
    }
    set_scl(0);
    ulp_i2c_delay();
  }
  // 发送 ACK / NACK
  set_sda(ack ? 0 : 1);
  ulp_i2c_delay();
  set_scl(1);
  ulp_i2c_delay();
  set_scl(0);
  ulp_i2c_delay();
  return data;
}

static bool soft_cst816s_read_reg(uint8_t reg, uint8_t *val) {
  // 1. 发送器件地址与要读取的寄存器
  soft_i2c_start();
  if (!soft_i2c_write_byte(CST816S_ADDR << 1)) {
    soft_i2c_stop();
    return false;
  }
  if (!soft_i2c_write_byte(reg)) {
    soft_i2c_stop();
    return false;
  }

  // 2. Restart 发送读命令并获取数据
  soft_i2c_start();
  if (!soft_i2c_write_byte((CST816S_ADDR << 1) | 0x01)) {
    soft_i2c_stop();
    return false;
  }
  *val = soft_i2c_read_byte(false); // 发送 NACK 结束单字节读取
  soft_i2c_stop();
  return true;
}

// =================【ULP 入口点】=================
int main(void) {
  uint8_t finger_num = 0;

  // 尝试读取 CST816S 寄存器 0x02 (手指数量)
  bool success = soft_cst816s_read_reg(REG_FINGER_NUM, &finger_num);

  // 如果成功读取且检测到有手指按下 (finger_num > 0)
  if (success && (finger_num > 0)) {
    // 唤醒 ESP32-S3 主 CPU！
    ulp_riscv_wakeup_main_processor();
  }

  // ULP 任务结束，进入等待状态，等待主 CPU 设置的下一个 20ms 定时周期
  return 0;
}