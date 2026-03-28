#include "esp32_s3_szp.h"
#include <stdio.h>

void app_main(void) {
  bsp_i2c_init();   // I2C初始化
  pca9557_init();   // IO扩展芯片初始化
  bsp_lvgl_start(); // 初始化液晶屏lvgl接口

  bsp_littlefs_mount(); // SPIFFS文件系统初始化
  bsp_codec_init();     // 音频初始化
}
