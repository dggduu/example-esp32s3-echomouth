#include "bsp_lcd.h"
#include "bsp_config.h"
#include "bsp_i2c.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_st77916.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"

#include "bsp_pca9539.h"

#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "misc/lv_color.h"
#include <stdbool.h>

static const char *TAG = "BSP_LCD";
static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_touch_handle_t s_touch = NULL;

static lv_disp_t *disp_handle = NULL;
static lv_indev_t *touch_indev = NULL;

static const st77916_lcd_init_cmd_t vendor_specific_init_yysj[] = {
    {0xF0, (uint8_t[]){0x28}, 1, 0},
    {0xF2, (uint8_t[]){0x28}, 1, 0},
    {0x73, (uint8_t[]){0xF0}, 1, 0},
    {0x7C, (uint8_t[]){0xD1}, 1, 0},
    {0x83, (uint8_t[]){0xE0}, 1, 0},
    {0x84, (uint8_t[]){0x61}, 1, 0},
    {0xF2, (uint8_t[]){0x82}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x01}, 1, 0},
    {0xF1, (uint8_t[]){0x01}, 1, 0},
    {0xB0, (uint8_t[]){0x56}, 1, 0},
    {0xB1, (uint8_t[]){0x4D}, 1, 0},
    {0xB2, (uint8_t[]){0x24}, 1, 0},
    {0xB4, (uint8_t[]){0x87}, 1, 0},
    {0xB5, (uint8_t[]){0x44}, 1, 0},
    {0xB6, (uint8_t[]){0x8B}, 1, 0},
    {0xB7, (uint8_t[]){0x40}, 1, 0},
    {0xB8, (uint8_t[]){0x86}, 1, 0},
    {0xBA, (uint8_t[]){0x00}, 1, 0},
    {0xBB, (uint8_t[]){0x08}, 1, 0},
    {0xBC, (uint8_t[]){0x08}, 1, 0},
    {0xBD, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x80}, 1, 0},
    {0xC1, (uint8_t[]){0x10}, 1, 0},
    {0xC2, (uint8_t[]){0x37}, 1, 0},
    {0xC3, (uint8_t[]){0x80}, 1, 0},
    {0xC4, (uint8_t[]){0x10}, 1, 0},
    {0xC5, (uint8_t[]){0x37}, 1, 0},
    {0xC6, (uint8_t[]){0xA9}, 1, 0},
    {0xC7, (uint8_t[]){0x41}, 1, 0},
    {0xC8, (uint8_t[]){0x01}, 1, 0},
    {0xC9, (uint8_t[]){0xA9}, 1, 0},
    {0xCA, (uint8_t[]){0x41}, 1, 0},
    {0xCB, (uint8_t[]){0x01}, 1, 0},
    {0xD0, (uint8_t[]){0x91}, 1, 0},
    {0xD1, (uint8_t[]){0x68}, 1, 0},
    {0xD2, (uint8_t[]){0x68}, 1, 0},
    {0xF5, (uint8_t[]){0x00, 0xA5}, 2, 0},
    {0xDD, (uint8_t[]){0x4F}, 1, 0},
    {0xDE, (uint8_t[]){0x4F}, 1, 0},
    {0xF1, (uint8_t[]){0x10}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x02}, 1, 0},
    {0xE0,
     (uint8_t[]){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29,
                 0x15, 0x15, 0x2E, 0x34},
     14, 0},
    {0xE1,
     (uint8_t[]){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39,
                 0x15, 0x15, 0x2D, 0x33},
     14, 0},
    {0xF0, (uint8_t[]){0x10}, 1, 0},
    {0xF3, (uint8_t[]){0x10}, 1, 0},
    {0xE0, (uint8_t[]){0x07}, 1, 0},
    {0xE1, (uint8_t[]){0x00}, 1, 0},
    {0xE2, (uint8_t[]){0x00}, 1, 0},
    {0xE3, (uint8_t[]){0x00}, 1, 0},
    {0xE4, (uint8_t[]){0xE0}, 1, 0},
    {0xE5, (uint8_t[]){0x06}, 1, 0},
    {0xE6, (uint8_t[]){0x21}, 1, 0},
    {0xE7, (uint8_t[]){0x01}, 1, 0},
    {0xE8, (uint8_t[]){0x05}, 1, 0},
    {0xE9, (uint8_t[]){0x02}, 1, 0},
    {0xEA, (uint8_t[]){0xDA}, 1, 0},
    {0xEB, (uint8_t[]){0x00}, 1, 0},
    {0xEC, (uint8_t[]){0x00}, 1, 0},
    {0xED, (uint8_t[]){0x0F}, 1, 0},
    {0xEE, (uint8_t[]){0x00}, 1, 0},
    {0xEF, (uint8_t[]){0x00}, 1, 0},
    {0xF8, (uint8_t[]){0x00}, 1, 0},
    {0xF9, (uint8_t[]){0x00}, 1, 0},
    {0xFA, (uint8_t[]){0x00}, 1, 0},
    {0xFB, (uint8_t[]){0x00}, 1, 0},
    {0xFC, (uint8_t[]){0x00}, 1, 0},
    {0xFD, (uint8_t[]){0x00}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xFF, (uint8_t[]){0x00}, 1, 0},
    {0x60, (uint8_t[]){0x40}, 1, 0},
    {0x61, (uint8_t[]){0x04}, 1, 0},
    {0x62, (uint8_t[]){0x00}, 1, 0},
    {0x63, (uint8_t[]){0x42}, 1, 0},
    {0x64, (uint8_t[]){0xD9}, 1, 0},
    {0x65, (uint8_t[]){0x00}, 1, 0},
    {0x66, (uint8_t[]){0x00}, 1, 0},
    {0x67, (uint8_t[]){0x00}, 1, 0},
    {0x68, (uint8_t[]){0x00}, 1, 0},
    {0x69, (uint8_t[]){0x00}, 1, 0},
    {0x6A, (uint8_t[]){0x00}, 1, 0},
    {0x6B, (uint8_t[]){0x00}, 1, 0},
    {0x70, (uint8_t[]){0x40}, 1, 0},
    {0x71, (uint8_t[]){0x03}, 1, 0},
    {0x72, (uint8_t[]){0x00}, 1, 0},
    {0x73, (uint8_t[]){0x42}, 1, 0},
    {0x74, (uint8_t[]){0xD8}, 1, 0},
    {0x75, (uint8_t[]){0x00}, 1, 0},
    {0x76, (uint8_t[]){0x00}, 1, 0},
    {0x77, (uint8_t[]){0x00}, 1, 0},
    {0x78, (uint8_t[]){0x00}, 1, 0},
    {0x79, (uint8_t[]){0x00}, 1, 0},
    {0x7A, (uint8_t[]){0x00}, 1, 0},
    {0x7B, (uint8_t[]){0x00}, 1, 0},
    {0x80, (uint8_t[]){0x48}, 1, 0},
    {0x81, (uint8_t[]){0x00}, 1, 0},
    {0x82, (uint8_t[]){0x06}, 1, 0},
    {0x83, (uint8_t[]){0x02}, 1, 0},
    {0x84, (uint8_t[]){0xD6}, 1, 0},
    {0x85, (uint8_t[]){0x04}, 1, 0},
    {0x86, (uint8_t[]){0x00}, 1, 0},
    {0x87, (uint8_t[]){0x00}, 1, 0},
    {0x88, (uint8_t[]){0x48}, 1, 0},
    {0x89, (uint8_t[]){0x00}, 1, 0},
    {0x8A, (uint8_t[]){0x08}, 1, 0},
    {0x8B, (uint8_t[]){0x02}, 1, 0},
    {0x8C, (uint8_t[]){0xD8}, 1, 0},
    {0x8D, (uint8_t[]){0x04}, 1, 0},
    {0x8E, (uint8_t[]){0x00}, 1, 0},
    {0x8F, (uint8_t[]){0x00}, 1, 0},
    {0x90, (uint8_t[]){0x48}, 1, 0},
    {0x91, (uint8_t[]){0x00}, 1, 0},
    {0x92, (uint8_t[]){0x0A}, 1, 0},
    {0x93, (uint8_t[]){0x02}, 1, 0},
    {0x94, (uint8_t[]){0xDA}, 1, 0},
    {0x95, (uint8_t[]){0x04}, 1, 0},
    {0x96, (uint8_t[]){0x00}, 1, 0},
    {0x97, (uint8_t[]){0x00}, 1, 0},
    {0x98, (uint8_t[]){0x48}, 1, 0},
    {0x99, (uint8_t[]){0x00}, 1, 0},
    {0x9A, (uint8_t[]){0x0C}, 1, 0},
    {0x9B, (uint8_t[]){0x02}, 1, 0},
    {0x9C, (uint8_t[]){0xDC}, 1, 0},
    {0x9D, (uint8_t[]){0x04}, 1, 0},
    {0x9E, (uint8_t[]){0x00}, 1, 0},
    {0x9F, (uint8_t[]){0x00}, 1, 0},
    {0xA0, (uint8_t[]){0x48}, 1, 0},
    {0xA1, (uint8_t[]){0x00}, 1, 0},
    {0xA2, (uint8_t[]){0x05}, 1, 0},
    {0xA3, (uint8_t[]){0x02}, 1, 0},
    {0xA4, (uint8_t[]){0xD5}, 1, 0},
    {0xA5, (uint8_t[]){0x04}, 1, 0},
    {0xA6, (uint8_t[]){0x00}, 1, 0},
    {0xA7, (uint8_t[]){0x00}, 1, 0},
    {0xA8, (uint8_t[]){0x48}, 1, 0},
    {0xA9, (uint8_t[]){0x00}, 1, 0},
    {0xAA, (uint8_t[]){0x07}, 1, 0},
    {0xAB, (uint8_t[]){0x02}, 1, 0},
    {0xAC, (uint8_t[]){0xD7}, 1, 0},
    {0xAD, (uint8_t[]){0x04}, 1, 0},
    {0xAE, (uint8_t[]){0x00}, 1, 0},
    {0xAF, (uint8_t[]){0x00}, 1, 0},
    {0xB0, (uint8_t[]){0x48}, 1, 0},
    {0xB1, (uint8_t[]){0x00}, 1, 0},
    {0xB2, (uint8_t[]){0x09}, 1, 0},
    {0xB3, (uint8_t[]){0x02}, 1, 0},
    {0xB4, (uint8_t[]){0xD9}, 1, 0},
    {0xB5, (uint8_t[]){0x04}, 1, 0},
    {0xB6, (uint8_t[]){0x00}, 1, 0},
    {0xB7, (uint8_t[]){0x00}, 1, 0},
    {0xB8, (uint8_t[]){0x48}, 1, 0},
    {0xB9, (uint8_t[]){0x00}, 1, 0},
    {0xBA, (uint8_t[]){0x0B}, 1, 0},
    {0xBB, (uint8_t[]){0x02}, 1, 0},
    {0xBC, (uint8_t[]){0xDB}, 1, 0},
    {0xBD, (uint8_t[]){0x04}, 1, 0},
    {0xBE, (uint8_t[]){0x00}, 1, 0},
    {0xBF, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x10}, 1, 0},
    {0xC1, (uint8_t[]){0x47}, 1, 0},
    {0xC2, (uint8_t[]){0x56}, 1, 0},
    {0xC3, (uint8_t[]){0x65}, 1, 0},
    {0xC4, (uint8_t[]){0x74}, 1, 0},
    {0xC5, (uint8_t[]){0x88}, 1, 0},
    {0xC6, (uint8_t[]){0x99}, 1, 0},
    {0xC7, (uint8_t[]){0x01}, 1, 0},
    {0xC8, (uint8_t[]){0xBB}, 1, 0},
    {0xC9, (uint8_t[]){0xAA}, 1, 0},
    {0xD0, (uint8_t[]){0x10}, 1, 0},
    {0xD1, (uint8_t[]){0x47}, 1, 0},
    {0xD2, (uint8_t[]){0x56}, 1, 0},
    {0xD3, (uint8_t[]){0x65}, 1, 0},
    {0xD4, (uint8_t[]){0x74}, 1, 0},
    {0xD5, (uint8_t[]){0x88}, 1, 0},
    {0xD6, (uint8_t[]){0x99}, 1, 0},
    {0xD7, (uint8_t[]){0x01}, 1, 0},
    {0xD8, (uint8_t[]){0xBB}, 1, 0},
    {0xD9, (uint8_t[]){0xAA}, 1, 0},
    {0xF3, (uint8_t[]){0x01}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0x21, (uint8_t[]){}, 0, 0},
    {0x11, (uint8_t[]){}, 0, 0},
    {0x29, (uint8_t[]){}, 0, 0},
    {0x00, (uint8_t[]){}, 0, 120},
};

// ===== 电源控制 =====

esp_err_t bsp_lcd_power_up(void) {
  ESP_LOGI(TAG, "LCD Power Up");

  /* CS 拉高 (未选中) */
  bsp_lcd_cs_high();

  /* 上电 */
  bsp_lcd_power_low();
  vTaskDelay(pdMS_TO_TICKS(10));

  /* 释放复位 */
  bsp_lcd_reset_high();
  vTaskDelay(pdMS_TO_TICKS(120));

  /* 触摸复位 */
  bsp_pca9539_set_pin_level(BSP_PCA9539_PIN(1, 1), 1); // TP_RST HIGH
  vTaskDelay(pdMS_TO_TICKS(10));

  /* 背光 */
  bsp_lcd_backlight_low();

  // bsp_i2c_scan(bsp_i2c_get_main_handle());
  ESP_LOGI(TAG, "LCD Power Up Done");
  return ESP_OK;
}

esp_err_t bsp_lcd_power_down(void) {
  ESP_LOGI(TAG, "LCD Power Down");

  // PMOS控制
  bsp_lcd_backlight_high();
  bsp_lcd_power_high();

  ESP_LOGI(TAG, "LCD Power Down Done");
  return ESP_OK;
}

void bsp_lcd_backlight_set(bool on) {
  if (on) {
    bsp_lcd_backlight_low();
  } else {
    bsp_lcd_backlight_high();
  }
}

// ===== LCD 初始化 (SPI + Panel + Touch) =====

esp_err_t bsp_lcd_init(esp_lcd_panel_handle_t *panel,
                       esp_lcd_touch_handle_t *touch) {
  ESP_LOGI(TAG, "LCD Init");

  /*---------------- CS 拉低 (选中) ----------------*/
  bsp_lcd_cs_low();

  /*---------------- SPI ----------------*/

  spi_bus_config_t buscfg = {
      .sclk_io_num = BSP_LCD_SCLK,
      .mosi_io_num = BSP_LCD_DATA0,
      .miso_io_num = BSP_LCD_DATA1,
      .quadwp_io_num = BSP_LCD_DATA2,
      .quadhd_io_num = BSP_LCD_DATA3,
      .max_transfer_sz = BSP_LCD_H_RES * 20 * sizeof(uint16_t),
  };

  ESP_ERROR_CHECK(spi_bus_initialize(BSP_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io_cfg = {

      .cs_gpio_num = GPIO_NUM_NC,
      .dc_gpio_num = GPIO_NUM_NC,

      .spi_mode = 0,

      .pclk_hz = LCD_PIXEL_CLOCK_HZ,

      .trans_queue_depth = 4,

      .lcd_cmd_bits = 32,
      .lcd_param_bits = 8,

      .flags =
          {
              .quad_mode = 1,
          },
  };

  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
      (esp_lcd_spi_bus_handle_t)BSP_LCD_HOST, &io_cfg, &s_io_handle));

  /*---------------- Panel ----------------*/

  st77916_vendor_config_t vendor = {

      .init_cmds = vendor_specific_init_yysj,

      .init_cmds_size =
          sizeof(vendor_specific_init_yysj) / sizeof(st77916_lcd_init_cmd_t),

      .flags =
          {
              .use_qspi_interface = 1,
          },
  };

  esp_lcd_panel_dev_config_t panel_cfg = {

      .reset_gpio_num = GPIO_NUM_NC,

      .bits_per_pixel = 16,

      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,

      .vendor_config = &vendor,
  };

  ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(s_io_handle, &panel_cfg, &s_panel));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));

  ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

  esp_lcd_panel_swap_xy(s_panel, false);

  esp_lcd_panel_mirror(s_panel, false, false);

  vTaskDelay(pdMS_TO_TICKS(20));

  /*---------------- Touch ----------------*/

  s_touch = NULL;

  i2c_master_bus_handle_t bus = bsp_i2c_get_main_handle();

  if (bus) {

    esp_lcd_panel_io_handle_t tp_io;

    esp_lcd_panel_io_i2c_config_t tp_io_cfg =
        ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();

    if (esp_lcd_new_panel_io_i2c(bus, &tp_io_cfg, &tp_io) == ESP_OK) {

      esp_lcd_touch_config_t tp_cfg = {

          .x_max = BSP_LCD_H_RES,
          .y_max = BSP_LCD_V_RES,

          .rst_gpio_num = BSP_TOUCH_RST,
          .int_gpio_num = BSP_TOUCH_INT,

          .flags =
              {

                  .mirror_x = 0,
                  .mirror_y = 0,
                  .swap_xy = 0,
              },
      };

      esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &s_touch);
    }
  }

  if (panel)
    *panel = s_panel;

  if (touch)
    *touch = s_touch;

  ESP_LOGI(TAG, "LCD Init Done");

  return ESP_OK;
}

// ===== 触摸 =====

esp_err_t bsp_lcd_touch_get_point(esp_lcd_touch_handle_t touch, uint16_t *x,
                                  uint16_t *y) {
  if (!touch || !x || !y)
    return ESP_ERR_INVALID_ARG;
  uint8_t touch_cnt = 0;
  esp_lcd_touch_point_data_t point[1];
  esp_lcd_touch_get_data(touch, point, &touch_cnt, 1);
  if (touch_cnt > 0) {
    *x = point[0].x;
    *y = point[0].y;
    return ESP_OK;
  }
  return ESP_ERR_NOT_FOUND;
}

// ===== RGB 测试 =====

esp_err_t bsp_lcd_rgb_test(esp_lcd_panel_handle_t panel) {
  if (panel == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  const int width = BSP_LCD_H_RES;
  const int height = 40;

  uint16_t *line =
      heap_caps_malloc(width * height * sizeof(uint16_t), MALLOC_CAP_INTERNAL);

  if (!line) {
    ESP_LOGE(TAG, "No memory");
    return ESP_ERR_NO_MEM;
  }

  const uint16_t colors[] = {
      0xF800, // Red
      0x07E0, // Green
      0x001F, // Blue
      0xFFE0, // Yellow
      0xF81F, // Magenta
      0x07FF, // Cyan
      0xFFFF, // White
      0x0000, // Black
  };

  const int color_num = sizeof(colors) / sizeof(colors[0]);

  for (int i = 0; i < color_num; i++) {

    for (int j = 0; j < width * height; j++) {
      line[j] = colors[i];
    }

    int y0 = i * height;
    int y1 = y0 + height;

    if (y1 > BSP_LCD_V_RES)
      y1 = BSP_LCD_V_RES;

    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y0, width, y1, line));
  }

  heap_caps_free(line);

  ESP_LOGI(TAG, "RGB test done");

  return ESP_OK;
}

// ===== LVGL 初始化 =====

esp_err_t bsp_lvgl_init(esp_lcd_panel_handle_t panel,
                        esp_lcd_touch_handle_t touch) {
  lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  lvgl_cfg.task_stack =
      16384; // Increase from default 7168 — needed for scaled image rendering
  ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

  /* -------- Display -------- */
  const lvgl_port_display_cfg_t disp_cfg = {
      .io_handle = s_io_handle,
      .panel_handle = panel,
      .control_handle = NULL,
      .buffer_size = BSP_LCD_H_RES *
                     20, // 7200px (~14.4KB) — larger tiles = fewer DMA txns
      .double_buffer = false,
      .hres = BSP_LCD_H_RES,
      .vres = BSP_LCD_V_RES,
      .monochrome = false,
      .color_format = LV_COLOR_FORMAT_RGB565_SWAPPED,
      .rotation =
          {
              .swap_xy = false,
              .mirror_x = false,
              .mirror_y = false,
          },
      .flags =
          {
              .buff_dma = true,
              .buff_spiram = false, // Internal DRAM avoids PSRAM DMA cache
                                    // coherency issues
          },
  };

  disp_handle = lvgl_port_add_disp(&disp_cfg);

  if (!disp_handle) {
    ESP_LOGE(TAG, "add display failed");
    return ESP_FAIL;
  }

  /* -------- Touch -------- */
  if (touch) {
    lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp_handle,
        .handle = touch,
        .scale =
            {
                .x = 1.0f,
                .y = 1.0f,
            },
    };

    touch_indev = lvgl_port_add_touch(&touch_cfg);

    if (!touch_indev) {
      ESP_LOGW(TAG, "touch init failed");
    } else {
      ESP_LOGI(TAG, "touch added");
    }
  }

  ESP_LOGI(TAG, "LVGL port initialized");
  return ESP_OK;
}

lv_indev_t *bsp_lcd_get_touch_indev(void) { return touch_indev; }
