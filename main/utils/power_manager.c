#include "power_manager.h"
#include "audio_helper.h"
#include "bsp_audio.h"
#include "bsp_board.h"
#include "bsp_camera.h"
#include "bsp_lcd.h"
#include "bsp_pca9539.h"
#include "cam_helper.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "lvgl.h"
#include "task_manager.h"
#include "ulp_riscv.h"
#include "ulp_riscv_i2c.h"
#include <stdbool.h>

/* ULP 二进制符号 — 由 ulp_embed_binary() 在链接时注入 */
extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[] asm("_binary_ulp_main_bin_end");

static const char *TAG = "PWR_MGR";

#define DIM_TIMEOUT_MS (3 * 60 * 1000) /* 3 分钟无操作 -> 进入 DIMMING */
#define DEEP_SLEEP_TIMEOUT_MS                                                  \
  (10 * 60 * 1000) /* 10 分钟 DIMMING -> 进入 Deep Sleep */

#define TOUCH_INT_GPIO GPIO_NUM_4

typedef enum {
  PWR_STATE_NORMAL = 0,
  PWR_STATE_DIMMING,
  PWR_STATE_DEEP_SLEEP,
} pwr_state_t;

static pwr_state_t s_state = PWR_STATE_NORMAL;
static TimerHandle_t s_pwr_check_timer = NULL;
static SemaphoreHandle_t s_mutex = NULL;

/* 引用外部全局触摸句柄（用于进入 Deep Sleep 前注销驱动） */
extern esp_lcd_touch_handle_t touch_handle;

#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_pm_lock = NULL;
#endif

static void pwr_lock(void) {
  if (!s_mutex)
    s_mutex = xSemaphoreCreateMutex();
  xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void pwr_unlock(void) { xSemaphoreGive(s_mutex); }

static bool should_block_sleep(void) {
  uint32_t cam_sub_count = cam_helper_get_subscriber_count();
  int32_t active_task_id = task_manager_get_active_id();

  if (cam_sub_count > 0) {
    ESP_LOGD(TAG, "Sleep blocked: Camera active subscribers = %" PRIu32,
             cam_sub_count);
    return true;
  }

  if (active_task_id != 0) {
    ESP_LOGD(TAG, "Sleep blocked: Active task running (ID = %" PRId32 ")",
             active_task_id);
    return true;
  }

  return false;
}

static void cpu_freq_low(void) {
#if CONFIG_PM_ENABLE
  if (s_pm_lock)
    esp_pm_lock_release(s_pm_lock);
#endif
}

static void cpu_freq_high(void) {
#if CONFIG_PM_ENABLE
  if (s_pm_lock)
    esp_pm_lock_acquire(s_pm_lock);
#endif
}

static void enter_dimming(void) {
  if (s_state == PWR_STATE_DIMMING)
    return;

  ESP_LOGI(TAG, "State -> DIMMING (Backlight OFF, CPU Low Freq)");
  bsp_lcd_backlight_set(false);
  cpu_freq_low();
  if (!audio_helper_is_running()) {
    bsp_pa_power_off();
  }
  s_state = PWR_STATE_DIMMING;
}

static void back_to_normal(void) {
  if (s_state == PWR_STATE_NORMAL)
    return;

  ESP_LOGI(TAG, "State -> NORMAL (Backlight ON, CPU High Freq)");
  bsp_lcd_backlight_set(true);
  cpu_freq_high();
  s_state = PWR_STATE_NORMAL;
}

/* 1 秒低频轮询，通过 LVGL 空闲检测实现 DIMMING 与 Deep Sleep 超时管理 */
static void pwr_check_timer_cb(TimerHandle_t timer) {
  pwr_lock();

  uint32_t inactive_ms = lv_display_get_inactive_time(NULL);

  if (inactive_ms < DIM_TIMEOUT_MS) {
    if (s_state != PWR_STATE_NORMAL) {
      back_to_normal();
    }
  } else if (inactive_ms >= DIM_TIMEOUT_MS &&
             inactive_ms < (DIM_TIMEOUT_MS + DEEP_SLEEP_TIMEOUT_MS)) {
    if (should_block_sleep()) {
      lv_display_trigger_activity(NULL);
      back_to_normal();
    } else if (s_state == PWR_STATE_NORMAL) {
      enter_dimming();
    }
  } else if (inactive_ms >= (DIM_TIMEOUT_MS + DEEP_SLEEP_TIMEOUT_MS)) {
    if (should_block_sleep()) {
      lv_display_trigger_activity(NULL);
      back_to_normal();
    } else {
      s_state = PWR_STATE_DEEP_SLEEP;
      pwr_unlock();
      power_manager_enter_deep_sleep();
      return;
    }
  }

  pwr_unlock();
}

/* ═══ Public APIs ═══ */

esp_err_t power_manager_init(void) {
  s_pwr_check_timer = xTimerCreate("pwr_chk_tmr", pdMS_TO_TICKS(1000), pdTRUE,
                                   NULL, pwr_check_timer_cb);

  if (!s_pwr_check_timer) {
    ESP_LOGE(TAG, "Failed to create power manager timer");
    return ESP_FAIL;
  }

#if CONFIG_PM_ENABLE
  esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "pwr_mgr", &s_pm_lock);
  if (s_pm_lock)
    esp_pm_lock_acquire(s_pm_lock);
#endif

  xTimerStart(s_pwr_check_timer, 0);
  ESP_LOGI(TAG, "Power Manager Init OK");
  return ESP_OK;
}

void power_manager_report_activity(void) {
  lv_display_trigger_activity(NULL);
  pwr_lock();
  back_to_normal();
  pwr_unlock();
}

void power_manager_enter_deep_sleep(void) {
  ESP_LOGI(TAG, "Preparing for Deep Sleep (ULP Soft-I2C touch wake)...");

  // 1. 外设断电
  bsp_pa_power_off();
  bsp_camera_power_down();
  bsp_audio_power_off();
  bsp_lcd_backlight_set(false);

  bsp_lcd_power_up();
  bsp_lcd_backlight_set(false);

  // 2. 注销主 CPU 的触摸驱动（释放 I2C 总线控制权）
  if (touch_handle) {
    ESP_LOGI(TAG, "De-initializing touch driver");
    esp_lcd_touch_del(touch_handle);
    touch_handle = NULL;
  }

  // 3. 将 GPIO5(SCL) 和 GPIO6(SDA) 配置为 RTC GPIO，供 ULP 进行软件模拟
  ESP_LOGI(TAG, "Configuring RTC GPIOs (SDA=6, SCL=5) for ULP Soft-I2C...");
  rtc_gpio_init(GPIO_NUM_5);
  rtc_gpio_init(GPIO_NUM_6);

  rtc_gpio_set_direction(GPIO_NUM_5, RTC_GPIO_MODE_INPUT_OUTPUT_OD);
  rtc_gpio_set_direction(GPIO_NUM_6, RTC_GPIO_MODE_INPUT_OUTPUT_OD);

  // 如果板子上有外部上拉电阻，可设为 false；若没有则开启芯片内部上拉
  rtc_gpio_pullup_en(GPIO_NUM_5);
  rtc_gpio_pullup_en(GPIO_NUM_6);

  // 4. 加载 ULP 程序
  ESP_LOGI(TAG, "Loading ULP touch-polling program...");
  esp_err_t err = ulp_riscv_load_binary(
      ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
  ESP_ERROR_CHECK(err);

  // 每 20ms 唤醒 ULP 执行一次软件轮询
  ulp_set_wakeup_period(0, 20000);

  // 5. 启动 ULP 并进入 Deep Sleep
  ESP_LOGI(TAG, "Starting ULP and entering Deep Sleep...");
  err = ulp_riscv_run();
  ESP_ERROR_CHECK(err);

  // 必须保持 RTC 外设域供电（ULP 和 RTC IO 正常运行所需）
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  ESP_LOGI(TAG,
           "Deep sleep started — ULP polling CST816S via Soft-I2C every 20ms");
  esp_deep_sleep_start();
}

void power_manager_exit_sleep(void) { power_manager_report_activity(); }

void power_manager_enter_sleep(void) { power_manager_enter_deep_sleep(); }

bool power_manager_is_deep_sleep_wakeup(void) {
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_ULP;
}

bool power_manager_is_screen_off(void) { return s_state >= PWR_STATE_DIMMING; }

bool power_manager_is_sleeping(void) { return s_state == PWR_STATE_DEEP_SLEEP; }