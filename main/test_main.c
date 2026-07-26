#include "bsp_i2c.h"
#include "bsp_pca9539.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_manager.h"
#include "ulp_riscv.h"

// 引入 ULP 编译自动生成的头文件
#include "ulp_main.h"

static const char *TAG = "POWER_MGR";

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[] asm("_binary_ulp_main_bin_end");

// 初始化并启动 ULP
static esp_err_t init_and_start_ulp(void) {
  // 1. 加载 ULP 二进制文件
  esp_err_t err = ulp_riscv_load_binary(
      ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "加载 ULP 固件失败: %s", esp_err_to_name(err));
    return err;
  }

  // 2. 配置 SDA(GPIO6) 和 SCL(GPIO5) 为 RTC GPIO 开漏输出 + 内部上拉
  rtc_gpio_init(GPIO_NUM_5);
  rtc_gpio_set_direction(GPIO_NUM_5, RTC_GPIO_MODE_INPUT_OUTPUT_OD);
  rtc_gpio_pullup_en(GPIO_NUM_5);

  rtc_gpio_init(GPIO_NUM_6);
  rtc_gpio_set_direction(GPIO_NUM_6, RTC_GPIO_MODE_INPUT_OUTPUT_OD);
  rtc_gpio_pullup_en(GPIO_NUM_6);

  // 3. 设置 ULP 唤醒周期（例如每 50ms 检查一次触摸，保证手感灵敏度与省电兼顾）
  ulp_set_wakeup_period(0, 50 * 1000); // 50ms (单位: 微秒)

  // 4. 启动 ULP RISC-V 协处理器
  err = ulp_riscv_run();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "启动 ULP 运行失败: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "ULP 轮询触摸唤醒程序启动成功！");
  return ESP_OK;
}

// 进入深度睡眠总入口
void power_manager_enter_deep_sleep(void) {
  ESP_LOGI(TAG, "准备进入休眠流程...");

  // ---------------- Step 1: PCA9539 准备电源与复位 ----------------
  // 确保主硬件 I2C 是可用状态
  bsp_i2c_init_main();
  bsp_pca9539_init(BSP_PCA9539_ADDR);

  // 关背光省电，但保持屏幕和触摸芯片供电
  bsp_lcd_backlight_low();
  bsp_lcd_power_low(); // 开启供电

  // 关键复位脉冲：强行将 CST816S TP_RST 拉低 20ms，再拉高
  ESP_LOGI(TAG, "对 CST816S 施加硬件复位脉冲...");
  bsp_lcd_touch_reset_low();
  vTaskDelay(pdMS_TO_TICKS(20));
  bsp_lcd_touch_reset_high();
  vTaskDelay(pdMS_TO_TICKS(50)); // 给触摸芯片留出内部初始化时间

  // ---------------- Step 2: 注销硬件 I2C 驱动 ----------------
  ESP_LOGI(TAG, "注销硬件 I2C，准备将引脚控制权交给 ULP...");
  bsp_i2c_deinit_main(); // 使用我们在上一步增加的 deinit 函数

  // ---------------- Step 3: 配置 ULP 与睡眠唤醒源 ----------------
  // 启动 ULP
  ESP_ERROR_CHECK(init_and_start_ulp());

  // 允许 ULP 唤醒主 CPU
  ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());

  ESP_LOGI(TAG, "系统即刻进入 Deep Sleep (触摸屏幕可唤醒)...");
  esp_deep_sleep_start();
}

// 检查唤醒原因
void power_manager_check_wakeup_cause(void) {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_ULP) {
    ESP_LOGI(TAG, "=== 系统成功被 ULP (触摸屏按下) 唤醒！===");
  } else {
    ESP_LOGI(TAG, "系统正常开机或通过其他源唤醒, Cause code: %d", cause);
  }
}