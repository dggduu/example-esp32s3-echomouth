#include "power_manager.h"
#include "audio_helper.h"
#include "cam_helper.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp32_s3_szp.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "task_manager.h"
#include "ulp_main.h"
#include "ulp_riscv.h"

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[] asm("_binary_ulp_main_bin_end");

static const char *TAG = "PWR_MGR";

#define ECO_TIMEOUT_MS (1 * 15 * 1000) /* 3 min idle → dim to 60% */
#define DIM_WARNING_MS 10000           /* 10 s dim warning → ECO   */
#define DEEP_SLEEP_TIMEOUT_MS                                                  \
  (1 * 20 * 1000) /* 10 min in ECO → deep sleep                              \
                   */

typedef enum {
  PWR_STATE_NORMAL = 0,
  PWR_STATE_DIMMING,    /* backlight 60%,  10 s countdown before ECO */
  PWR_STATE_ECO,        /* backlight off,  CPU freq min, PA off     */
  PWR_STATE_DEEP_SLEEP, /* ULP RISC-V monitoring GPIO0               */
} pwr_state_t;

static pwr_state_t s_state = PWR_STATE_NORMAL;
static TimerHandle_t s_eco_timer = NULL;
static TimerHandle_t s_dim_timer = NULL;
static TimerHandle_t s_deep_sleep_timer = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static bool s_ulp_loaded = false;

#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_pm_lock = NULL;
#endif

/* ── helpers ── */
static void pwr_lock(void) {
  if (s_mutex == NULL)
    s_mutex = xSemaphoreCreateMutex();
  xSemaphoreTake(s_mutex, portMAX_DELAY);
}
static void pwr_unlock(void) { xSemaphoreGive(s_mutex); }
static void load_ulp_program(void);

static bool should_block(void) {
  return (task_manager_get_active_id() != 0) || cam_helper_is_running();
}

/* ── CPU freq control ── */
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

/* ── Step 1: NORMAL → DIMMING  (backlight 60%) ── */
static void enter_dim(void) {
  ESP_LOGI(TAG, "→ DIMMING (backlight 30%%, 10 s warning)");
  bsp_display_brightness_set(30);
  if (s_dim_timer)
    xTimerStart(s_dim_timer, 0);
}

/* ── Step 2: DIMMING → ECO  (backlight off, lower freq, PA off) ── */
static void enter_eco(void) {
  ESP_LOGI(TAG, "→ ECO (backlight off, CPU freq min)");
  bsp_display_brightness_set(0);
  cpu_freq_low();
  if (!audio_helper_is_running())
    pa_en(0);
  if (s_deep_sleep_timer)
    xTimerStart(s_deep_sleep_timer, 0);
}

/* ── Any non-DEEP_SLEEP → NORMAL ── */
static void back_to_normal(void) {
  if (s_state == PWR_STATE_NORMAL)
    return;
  ESP_LOGI(TAG, "→ NORMAL (full brightness, max freq)");
  bsp_display_brightness_set(100);
  cpu_freq_high();
  s_state = PWR_STATE_NORMAL;
  if (s_dim_timer)
    xTimerStop(s_dim_timer, 0);
  if (s_deep_sleep_timer)
    xTimerStop(s_deep_sleep_timer, 0);
}

/* ══════════════════════════════════════════════════
   Timer callbacks
   ══════════════════════════════════════════════════ */

/* 3-min ECO timer: NORMAL → DIMMING */
static void eco_timer_cb(TimerHandle_t timer) {
  pwr_lock();
  if (s_state == PWR_STATE_NORMAL) {
    if (should_block()) {
      ESP_LOGI(TAG, "ECO blocked, restart timer");
      xTimerStart(s_eco_timer, 0);
    } else {
      s_state = PWR_STATE_DIMMING;
      pwr_unlock();
      enter_dim();
      return;
    }
  }
  pwr_unlock();
}

/* 10-s dim warning timer: DIMMING → ECO */
static void dim_timer_cb(TimerHandle_t timer) {
  pwr_lock();
  if (s_state == PWR_STATE_DIMMING) {
    if (should_block()) {
      ESP_LOGI(TAG, "ECO blocked during dim warning, back to NORMAL");
      back_to_normal();
      xTimerStart(s_eco_timer, 0);
    } else {
      s_state = PWR_STATE_ECO;
      pwr_unlock();
      enter_eco();
      return;
    }
  }
  pwr_unlock();
}

/* 10-min deep sleep timer: ECO → DEEP_SLEEP */
static void deep_sleep_timer_cb(TimerHandle_t timer) {
  pwr_lock();
  if (s_state == PWR_STATE_ECO) {
    if (should_block()) {
      ESP_LOGI(TAG, "Deep sleep blocked, restart timer");
      xTimerStart(s_deep_sleep_timer, 0);
    } else {
      ESP_LOGI(TAG, "→ DEEP_SLEEP with ULP");
      s_state = PWR_STATE_DEEP_SLEEP;
      pwr_unlock();

      load_ulp_program();

      bsp_display_brightness_set(0);
      rtc_gpio_deinit(GPIO_NUM_0);
      rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_en(GPIO_NUM_0);

      ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
      ESP_LOGI(TAG, "Entering deep sleep, ULP monitoring GPIO0...");
      esp_deep_sleep_start();
      return;
    }
  }
  pwr_unlock();
}

/* ── Load ULP RISC-V binary ── */
static void load_ulp_program(void) {
  if (s_ulp_loaded)
    return;

  esp_err_t err = ulp_riscv_load_binary(
      ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to load ULP: %s", esp_err_to_name(err));
    return;
  }
  ulp_set_wakeup_period(0, 20000);
  err = ulp_riscv_run();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start ULP: %s", esp_err_to_name(err));
    return;
  }
  s_ulp_loaded = true;
  ESP_LOGI(TAG, "ULP program loaded and running");
}

/* ══════════════════════════════════════════════════
   Public API
   ══════════════════════════════════════════════════ */

esp_err_t power_manager_init(void) {
  s_eco_timer = xTimerCreate("eco_tmr", pdMS_TO_TICKS(ECO_TIMEOUT_MS), pdFALSE,
                             NULL, eco_timer_cb);
  if (!s_eco_timer) {
    ESP_LOGE(TAG, "eco_tmr failed");
    return ESP_FAIL;
  }

  s_dim_timer = xTimerCreate("dim_tmr", pdMS_TO_TICKS(DIM_WARNING_MS), pdFALSE,
                             NULL, dim_timer_cb);
  if (!s_dim_timer) {
    ESP_LOGE(TAG, "dim_tmr failed");
    return ESP_FAIL;
  }

  s_deep_sleep_timer =
      xTimerCreate("ds_tmr", pdMS_TO_TICKS(DEEP_SLEEP_TIMEOUT_MS), pdFALSE,
                   NULL, deep_sleep_timer_cb);
  if (!s_deep_sleep_timer) {
    ESP_LOGE(TAG, "ds_tmr failed");
    return ESP_FAIL;
  }

#if CONFIG_PM_ENABLE
  esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "pwr_mgr", &s_pm_lock);
  if (s_pm_lock)
    esp_pm_lock_acquire(s_pm_lock);
#endif

  xTimerStart(s_eco_timer, 0);
  ESP_LOGI(TAG, "Init: eco=%ds  dim=%ds  deep=%ds", ECO_TIMEOUT_MS / 1000,
           DIM_WARNING_MS / 1000, DEEP_SLEEP_TIMEOUT_MS / 1000);
  return ESP_OK;
}

void power_manager_report_activity(void) {
  pwr_lock();
  if (s_state == PWR_STATE_DIMMING || s_state == PWR_STATE_ECO) {
    back_to_normal();
  }
  if (s_state == PWR_STATE_NORMAL && s_eco_timer) {
    xTimerReset(s_eco_timer, 0);
  }
  pwr_unlock();
}

void power_manager_enter_sleep(void) {
  pwr_lock();
  if (s_state != PWR_STATE_DEEP_SLEEP) {
    ESP_LOGI(TAG, "Force → DEEP_SLEEP");
    s_state = PWR_STATE_DEEP_SLEEP;
    pwr_unlock();

    load_ulp_program();
    bsp_display_brightness_set(0);
    rtc_gpio_deinit(GPIO_NUM_0);
    rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(GPIO_NUM_0);
    esp_sleep_enable_ulp_wakeup();
    esp_deep_sleep_start();
    return;
  }
  pwr_unlock();
}

void power_manager_exit_sleep(void) { power_manager_report_activity(); }
void power_manager_enter_deep_sleep(void) { power_manager_enter_sleep(); }
bool power_manager_is_deep_sleep_wakeup(void) {
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_ULP;
}
bool power_manager_is_screen_off(void) { return s_state >= PWR_STATE_ECO; }
bool power_manager_is_sleeping(void) { return s_state == PWR_STATE_DEEP_SLEEP; }
