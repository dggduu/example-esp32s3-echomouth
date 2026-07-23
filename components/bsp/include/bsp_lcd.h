#ifndef BSP_LCD_H
#define BSP_LCD_H

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_TRANS_QUEUE_DEPTH 10
#define LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)

/* ---- 电源 / 背光 ---- */
esp_err_t bsp_lcd_power_up(void);
esp_err_t bsp_lcd_power_down(void);
void bsp_lcd_backlight_set(bool on);

/* ---- 初始化 (power_up 之后调用) ---- */
esp_err_t bsp_lcd_init(esp_lcd_panel_handle_t *lcd_panel,
                       esp_lcd_touch_handle_t *touch_handle);

/* ---- 触摸 ---- */
esp_err_t bsp_lcd_touch_get_point(esp_lcd_touch_handle_t touch, uint16_t *x,
                                  uint16_t *y);

/* ---- 测试 ---- */
esp_err_t bsp_lcd_rgb_test(esp_lcd_panel_handle_t panel);

/* ---- LVGL ---- */
esp_err_t bsp_lvgl_init(esp_lcd_panel_handle_t panel,
                        esp_lcd_touch_handle_t touch);
lv_indev_t *bsp_lcd_get_touch_indev(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_LCD_H
