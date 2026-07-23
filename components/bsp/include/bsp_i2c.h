#ifndef BSP_I2C_H
#define BSP_I2C_H

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 主总线
esp_err_t bsp_i2c_init_main(void);
i2c_master_bus_handle_t bsp_i2c_get_main_handle(void);

// 摄像头总线 (I2C_NUM_1, GPIO8/18)
esp_err_t bsp_i2c_init_cam(void);
i2c_master_bus_handle_t bsp_i2c_get_cam_handle(void);

// 摄像头传感器寄存器读写（16位寄存器地址）
esp_err_t bsp_i2c_read_cam_reg(uint8_t addr_7bit, uint16_t reg,
                               uint8_t *value);
esp_err_t bsp_i2c_write_cam_reg(uint8_t addr_7bit, uint16_t reg,
                                uint8_t value);

// 电池总线（新版）
esp_err_t bsp_i2c_init_bat(void);
i2c_master_bus_handle_t bsp_i2c_get_bat_handle(void);

// 主总线读写
esp_err_t bsp_i2c_write_main(uint8_t addr_7bit, const uint8_t *data, size_t len,
                             int timeout_ms);
esp_err_t bsp_i2c_read_main(uint8_t addr_7bit, uint8_t *data, size_t len,
                            int timeout_ms);
esp_err_t bsp_i2c_write_read_main(uint8_t addr_7bit, const uint8_t *write_data,
                                  size_t write_len, uint8_t *read_data,
                                  size_t read_len, int timeout_ms);

// 电池总线读写
esp_err_t bsp_i2c_write_bat(uint8_t addr_7bit, const uint8_t *data, size_t len,
                            int timeout_ms);
esp_err_t bsp_i2c_read_bat(uint8_t addr_7bit, uint8_t *data, size_t len,
                           int timeout_ms);
esp_err_t bsp_i2c_write_read_bat(uint8_t addr_7bit, const uint8_t *write_data,
                                 size_t write_len, uint8_t *read_data,
                                 size_t read_len, int timeout_ms);

void bsp_i2c_scan(i2c_master_bus_handle_t bus);

#ifdef __cplusplus
}
#endif

#endif // BSP_I2C_H