#ifndef BSP_CONFIG_H
#define BSP_CONFIG_H

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_types.h"
#include "driver/spi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// ===== I2C 总线（对应 GPIO5/6 主总线，GPIO8/18 电池总线） =====
#define BSP_I2C_MAIN_PORT I2C_NUM_0
#define BSP_I2C_MAIN_SCL GPIO_NUM_5
#define BSP_I2C_MAIN_SDA GPIO_NUM_6
#define BSP_I2C_MAIN_FREQ 100000

#define BSP_I2C_CAM_PORT I2C_NUM_1
#define BSP_I2C_CAM_SCL GPIO_NUM_8  // 对应: GC_I2C_SCL
#define BSP_I2C_CAM_SDA GPIO_NUM_18 // 对应: GC_I2C_SDA
#define BSP_I2C_BAT_FREQ 100000

// ===== I2S 音频 =====
#define BSP_I2S_PORT I2S_NUM_0
#define BSP_I2S_MCLK GPIO_NUM_17
#define BSP_I2S_BCK GPIO_NUM_7
#define BSP_I2S_WS GPIO_NUM_15
#define BSP_I2S_DIN GPIO_NUM_3
#define BSP_I2S_DOUT GPIO_NUM_16

// ===== LCD 显示 =====
#define BSP_LCD_HOST SPI2_HOST
#define BSP_LCD_SCLK GPIO_NUM_2 // 对应: LCD_SCL
#define BSP_LCD_CS GPIO_NUM_NC  //
#define BSP_LCD_DC GPIO_NUM_NC  //
#define BSP_LCD_BLK GPIO_NUM_NC //
#define BSP_LCD_RST GPIO_NUM_NC //

#define BSP_LCD_DATA0 GPIO_NUM_42 // 对应: LCD_SDA0
#define BSP_LCD_DATA1 GPIO_NUM_41 // 对应: LCD_SDA1
#define BSP_LCD_DATA2 GPIO_NUM_40 // 对应: LCD_SDA2
#define BSP_LCD_DATA3 GPIO_NUM_39 // 对应: LCD_SDA3

#define BSP_LCD_H_RES 360
#define BSP_LCD_V_RES 360
#define BSP_LCD_BPP 16

// ===== 触摸 =====
#define BSP_TOUCH_INT GPIO_NUM_1
#define BSP_TOUCH_RST GPIO_NUM_NC

// ===== 摄像头 (OV2640 等) =====
#define BSP_CAM_D0 GPIO_NUM_11
#define BSP_CAM_D1 GPIO_NUM_9
#define BSP_CAM_D2 GPIO_NUM_46
#define BSP_CAM_D3 GPIO_NUM_10
#define BSP_CAM_D4 GPIO_NUM_12
#define BSP_CAM_D5 GPIO_NUM_14
#define BSP_CAM_D6 GPIO_NUM_21
#define BSP_CAM_D7 GPIO_NUM_48
#define BSP_CAM_HSYNC GPIO_NUM_45
#define BSP_CAM_VSYNC GPIO_NUM_38
#define BSP_CAM_MCLK GPIO_NUM_47
#define BSP_CAM_PCLK GPIO_NUM_13
#define BSP_CAM_PWDN GPIO_NUM_NC
#define BSP_CAM_RESET GPIO_NUM_NC

// ===== 设备 7-bit I2C 地址 =====
#define BSP_PCA9539_ADDR 0x74 // A0/A1/A2 均接地
#define BSP_ES8311_ADDR (0x18 << 1)
#define BSP_ES7210_ADDR (0x40 << 1)
// #define BSP_CST816S_ADDR 0x74
#define BSP_BQ27220_ADDR 0x55

#define BSP_PIN_PA_CTRL BSP_PCA9539_PIN(0, 0)
#define BSP_IOEXP_AUDIO_PWR_EN BSP_PCA9539_PIN(0, 5)
#define BSP_IOEXP_CAM_EN BSP_PCA9539_PIN(0, 6)

#define BSP_IOEXP_LCD_CS BSP_PCA9539_PIN(1, 2)
#define BSP_IOEXP_LCD_RST BSP_PCA9539_PIN(1, 3)
#define BSP_IOEXP_SCREEN_PWR BSP_PCA9539_PIN(1, 5)
#define BSP_IOEXP_CAM_RST BSP_PCA9539_PIN(1, 0)
#define BSP_IOEXP_TP_RST BSP_PCA9539_PIN(1, 1)
#define BSP_IOEXP_BLC BSP_PCA9539_PIN(1, 4)
#define BSP_IOEXP_SCREEN_CS BSP_PCA9539_PIN(1, 2)

#ifdef __cplusplus
}
#endif

#endif // BSP_CONFIG_H