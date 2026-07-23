#ifndef BSP_PCA9539_H
#define BSP_PCA9539_H

#include "bsp_config.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pin 编号：
 *
 * 0~7   -> Port0
 * 8~15  -> Port1
 */

#define BSP_PCA9539_PIN(PORT_NUM, BIT_NUM)                                     \
  ((((PORT_NUM) & 0x01) << 3) | ((BIT_NUM) & 0x07))

/*-------------------- 常用设备控制 --------------------*/

#define bsp_pa_power_on() bsp_pca9539_set_pin_level(BSP_PIN_PA_CTRL, true)

#define bsp_pa_power_off() bsp_pca9539_set_pin_level(BSP_PIN_PA_CTRL, false)

#define bsp_audio_power_on()                                                   \
  bsp_pca9539_set_pin_level(BSP_IOEXP_AUDIO_PWR_EN, true)

#define bsp_audio_power_off()                                                  \
  bsp_pca9539_set_pin_level(BSP_IOEXP_AUDIO_PWR_EN, false)

#define bsp_cam_power_on() bsp_pca9539_set_pin_level(BSP_IOEXP_CAM_EN, true)

#define bsp_cam_power_off() bsp_pca9539_set_pin_level(BSP_IOEXP_CAM_EN, false)

#define bsp_lcd_reset_high()                                                   \
  bsp_pca9539_set_pin_level(BSP_PCA9539_PIN(1, BSP_IOEXP_LCD_RST), true)

#define bsp_lcd_reset_low()                                                    \
  bsp_pca9539_set_pin_level(BSP_PCA9539_PIN(1, BSP_IOEXP_LCD_RST), false)

#define bsp_lcd_touch_reset_high()                                                   \
  bsp_pca9539_set_pin_level(BSP_PCA9539_PIN(1, BSP_IOEXP_LCD_RST), true)

#define bsp_lcd_touch_reset_low()                                                    \
  bsp_pca9539_set_pin_level(BSP_PCA9539_PIN(1, BSP_IOEX), false)

#define bsp_lcd_power_high()                                                   \
  bsp_pca9539_set_pin_level(BSP_PCA9539_PIN(1, BSP_IOEXP_SCREEN_PWR), true)

#define bsp_lcd_power_low()                                                    \
  bsp_pca9539_set_pin_level(BSP_PCA9539_PIN(1, BSP_IOEXP_SCREEN_PWR), false)

#define bsp_lcd_backlight_high()                                               \
  bsp_pca9539_set_pin_level(BSP_PCA9539_PIN(1, BSP_IOEXP_BLC), true)

#define bsp_lcd_backlight_low()                                                \
  bsp_pca9539_set_pin_level(BSP_PCA9539_PIN(1, BSP_IOEXP_BLC), false)

#define bsp_lcd_cs_high()                                                      \
  bsp_pca9539_set_pin_level(BSP_PCA9539_PIN(1, BSP_IOEXP_LCD_CS), true)

#define bsp_lcd_cs_low()                                                       \
  bsp_pca9539_set_pin_level(BSP_PCA9539_PIN(1, BSP_IOEXP_LCD_CS), false)

/*-------------------- 初始化 --------------------*/

esp_err_t bsp_pca9539_init(uint8_t addr);

/*-------------------- 单IO --------------------*/

esp_err_t bsp_pca9539_set_pin_direction(uint8_t pin, bool input);

esp_err_t bsp_pca9539_set_pin_level(uint8_t pin, bool level);

esp_err_t bsp_pca9539_get_pin_level(uint8_t pin, bool *level);

/*-------------------- Port --------------------*/

esp_err_t bsp_pca9539_write_output_port0(uint8_t value);

esp_err_t bsp_pca9539_write_output_port1(uint8_t value);

esp_err_t bsp_pca9539_read_input_port0(uint8_t *value);

esp_err_t bsp_pca9539_read_input_port1(uint8_t *value);

esp_err_t bsp_pca9539_set_direction_port0(uint8_t value);

esp_err_t bsp_pca9539_set_direction_port1(uint8_t value);

#ifdef __cplusplus
}
#endif

#endif