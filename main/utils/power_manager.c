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
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "task_manager.h"

static const char *TAG = "PWR_MGR";

#define ECO_TIMEOUT_MS (3 * 60 * 1000) /* 3 分钟空闲 -> 进入 DIMMING */
#define DIM_WARNING_MS (10 * 1000)     /* 10 秒微暗警告 -> 进入 ECO */
#define DEEP_SLEEP_TIMEOUT_MS                                                  \
  (10 * 60 * 1000)               /* 10 分钟 ECO -> 进入 Deep Sleep */
#define LIGHT_SLEEP_DURATION_S 5 /* ECO 模式下的 Light Sleep 轮询周期 */

#define TOUCH_INT_GPIO GPIO_NUM_4

typedef enum {
  PWR_STATE_NORMAL = 0,
  PWR_STATE_DIMMING,
  PWR_STATE_ECO,
  PWR_STATE_DEEP_SLEEP,
} pwr_state_t;

static pwr_state_t s_state = PWR_STATE_NORMAL;
static TimerHandle_t s_eco_timer = NULL;
static TimerHandle_t s_dim_timer = NULL;
static TimerHandle_t s_deep_sleep_timer = NULL;
static SemaphoreHandle_t s_mutex = NULL;

#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_pm_lock = NULL;
#endif

static void pwr_lock(void) {
  if (!s_mutex)
    s_mutex = xSemaphoreCreateMutex();
  xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void pwr_unlock(void) { xSemaphoreGive(s_mutex); }

/* ---------------------------------------------------------------
 * 检查当前是否有业务活动阻碍系统进入休眠
 * 1. 摄像头订阅数 > 0
 * 2. Task Manager 中有正在执行的 Active Task (ID != 0)
 * --------------------------------------------------------------*/
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

/* ---------------------------------------------------------------
 * 硬件低功耗预置：
 * 关闭音频 PA、摄像头电源，LCD 背光关断，但保持 LCD 供电 (使能 Touch IC)
 * --------------------------------------------------------------*/
static void configure_hardware_for_deep_sleep(void) {
  bsp_pa_power_off();
  bsp_camera_power_down();
  bsp_audio_power_off();

  // 背光关断，屏幕电源维持开启（保障触摸芯片 CST816S 持续供电）
  bsp_lcd_backlight_set(false);
  bsp_lcd_power_up();

  ESP_LOGI(TAG, "Hardware pre-configured for Deep Sleep");
}

/* ── NORMAL → DIMMING ── */
static void enter_dim(void) {
  ESP_LOGI(TAG, "State -> DIMMING");
  bsp_lcd_backlight_set(true);
  if (s_dim_timer)
    xTimerStart(s_dim_timer, 0);
}

/* ── DIMMING → ECO ── */
static void enter_eco(void) {
  ESP_LOGI(TAG, "State -> ECO (Backlight OFF, CPU Low Freq)");
  bsp_lcd_backlight_set(false);
  cpu_freq_low();
  if (!audio_helper_is_running()) {
    bsp_pa_power_off();
  }
  if (s_deep_sleep_timer)
    xTimerStart(s_deep_sleep_timer, 0);
}

/* ── ANY → NORMAL ── */
static void back_to_normal(void) {
  if (s_state == PWR_STATE_NORMAL)
    return;

  ESP_LOGI(TAG, "State -> NORMAL");
  bsp_lcd_backlight_set(true);
  cpu_freq_high();
  s_state = PWR_STATE_NORMAL;

  if (s_dim_timer)
    xTimerStop(s_dim_timer, 0);
  if (s_deep_sleep_timer)
    xTimerStop(s_deep_sleep_timer, 0);
  if (s_eco_timer)
    xTimerReset(s_eco_timer, 0);
}

/* ── ECO 模式下的 Light Sleep 循环 ── */
static void eco_tickless_doze(void) {
  /* 允许 GPIO4 (TP_INT) 低电平唤醒和定时器唤醒 */
  gpio_wakeup_enable(TOUCH_INT_GPIO, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup((uint64_t)LIGHT_SLEEP_DURATION_S * 1000000ULL);

  esp_light_sleep_start();

  /* 检查唤醒源 */
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_GPIO) {
    ESP_LOGI(TAG, "Light sleep wake: Touch INT (GPIO4) -> NORMAL");
    pwr_lock();
    back_to_normal();
    pwr_unlock();
  }
}

/* ═══ 定时器回调 ═══ */

static void eco_timer_cb(TimerHandle_t timer) {
  pwr_lock();
  if (s_state == PWR_STATE_NORMAL) {
    if (should_block_sleep()) {
      ESP_LOGI(TAG, "ECO blocked by camera/task, restarting timer");
      xTimerStart(s_eco_timer, 0);
    } else {
      s_state = PWR_STATE_DIMMING;
      enter_dim();
    }
  }
  pwr_unlock();
}

static void dim_timer_cb(TimerHandle_t timer) {
  pwr_lock();
  if (s_state == PWR_STATE_DIMMING) {
    if (should_block_sleep()) {
      ESP_LOGI(TAG, "ECO blocked in DIMMING, returning to NORMAL");
      back_to_normal();
    } else {
      s_state = PWR_STATE_ECO;
      enter_eco();
      pwr_unlock();

      /* 循环进入 Light Sleep 降低空闲功耗 */
      while (1) {
        pwr_lock();
        bool stay_in_eco = (s_state == PWR_STATE_ECO) && !should_block_sleep();
        pwr_unlock();

        if (!stay_in_eco)
          break;
        eco_tickless_doze();
      }
      return;
    }
  }
  pwr_unlock();
}

static void deep_sleep_timer_cb(TimerHandle_t timer) {
  pwr_lock();
  if (s_state == PWR_STATE_ECO) {
    if (should_block_sleep()) {
      ESP_LOGI(TAG, "Deep Sleep blocked, restarting timer");
      xTimerStart(s_deep_sleep_timer, 0);
      pwr_unlock();
    } else {
      s_state = PWR_STATE_DEEP_SLEEP;
      pwr_unlock();

      power_manager_enter_deep_sleep();
    }
  } else {
    pwr_unlock();
  }
}

/* ═══ Public APIs ═══ */

esp_err_t power_manager_init(void) {
  s_eco_timer = xTimerCreate("eco_tmr", pdMS_TO_TICKS(ECO_TIMEOUT_MS), pdFALSE,
                             NULL, eco_timer_cb);
  s_dim_timer = xTimerCreate("dim_tmr", pdMS_TO_TICKS(DIM_WARNING_MS), pdFALSE,
                             NULL, dim_timer_cb);
  s_deep_sleep_timer =
      xTimerCreate("ds_tmr", pdMS_TO_TICKS(DEEP_SLEEP_TIMEOUT_MS), pdFALSE,
                   NULL, deep_sleep_timer_cb);

  if (!s_eco_timer || !s_dim_timer || !s_deep_sleep_timer) {
    ESP_LOGE(TAG, "Failed to create power manager timers");
    return ESP_FAIL;
  }

#if CONFIG_PM_ENABLE
  esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "pwr_mgr", &s_pm_lock);
  if (s_pm_lock)
    esp_pm_lock_acquire(s_pm_lock);
#endif

  xTimerStart(s_eco_timer, 0);
  ESP_LOGI(TAG, "Power Manager Init OK");
  return ESP_OK;
}

void power_manager_report_activity(void) {
  pwr_lock();
  if (s_state == PWR_STATE_DIMMING || s_state == PWR_STATE_ECO) {
    back_to_normal();
  } else if (s_state == PWR_STATE_NORMAL && s_eco_timer) {
    xTimerReset(s_eco_timer, 0);
  }
  pwr_unlock();
}

void power_manager_enter_deep_sleep(void) {
  ESP_LOGI(TAG, "Entering Deep Sleep via EXT0 (GPIO4 Low Level)...");

  // 1. 关闭无关外设
  configure_hardware_for_deep_sleep();

  // 2. 配置 RTC GPIO4 作为低电平唤醒源
  rtc_gpio_init(TOUCH_INT_GPIO);
  rtc_gpio_set_direction(TOUCH_INT_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(TOUCH_INT_GPIO);
  rtc_gpio_pulldown_dis(TOUCH_INT_GPIO);

  // 3. 设置 EXT0 唤醒：低电平触发 (0)
  esp_sleep_enable_ext0_wakeup(TOUCH_INT_GPIO, 0);

  // 4. 保持 RTC 域供电
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  // 5. 正式进入 Deep Sleep
  esp_deep_sleep_start();
}

void power_manager_exit_sleep(void) { power_manager_report_activity(); }

void power_manager_enter_sleep(void) { power_manager_enter_deep_sleep(); }

bool power_manager_is_deep_sleep_wakeup(void) {
  // EXT0 触发唤醒说明是由 GPIO4 (触摸触控) 唤醒的
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
}

bool power_manager_is_screen_off(void) { return s_state >= PWR_STATE_ECO; }

bool power_manager_is_sleeping(void) { return s_state == PWR_STATE_DEEP_SLEEP; }