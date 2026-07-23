#include "bsp_camera.h"
#include "bsp_config.h"
#include "bsp_pca9539.h"
#include "driver/ledc.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/ledc_types.h"
#include "sensor.h"

static const char *TAG = "BSP_CAMERA";
static bool s_powered = false;
static bool s_initialized = false;

/* ---- 电源 + 时钟 ---- */

esp_err_t bsp_camera_power_up(void) {
  if (s_powered) {
    ESP_LOGW(TAG, "Camera already powered up");
    return ESP_OK;
  }

  /* 上电 */
  bsp_cam_power_on();
  vTaskDelay(pdMS_TO_TICKS(10));

  /* 释放复位 */
  bsp_pca9539_set_pin_level(BSP_IOEXP_CAM_RST, true);
  vTaskDelay(pdMS_TO_TICKS(50));

  /* XCLK (MCLK) — esp_camera_init 也会配置 LEDC，这里提前启动确保 I2C 可用 */
  ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_1_BIT,
      .timer_num = LEDC_TIMER_1,
      .freq_hz = 10 * 1000 * 1000,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&timer));

  ledc_channel_config_t ch = {
      .gpio_num = BSP_CAM_MCLK,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_1,
      .timer_sel = LEDC_TIMER_1,
      .duty = 1,
      .hpoint = 0,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&ch));

  s_powered = true;
  ESP_LOGI(TAG, "Power up OK (XCLK=10MHz on GPIO%d)", BSP_CAM_MCLK);
  return ESP_OK;
}

esp_err_t bsp_camera_power_down(void) {
  if (!s_powered)
    return ESP_OK;

  /* 如果已初始化，先反初始化 */
  if (s_initialized) {
    bsp_camera_deinit();
  }

  /* 停止 XCLK */
  ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
  gpio_reset_pin(BSP_CAM_MCLK);

  /* 断电 */
  bsp_cam_power_off();

  s_powered = false;
  ESP_LOGI(TAG, "Power down OK");
  return ESP_OK;
}

/* ---- 初始化 / 反初始化 ---- */

esp_err_t bsp_camera_init(uint32_t xclk_freq_hz, pixformat_t pixel_format,
                          framesize_t frame_size, uint8_t fb_count) {
  if (s_initialized) {
    ESP_LOGW(TAG, "Camera already initialized");
    return ESP_OK;
  }

  if (!s_powered) {
    ESP_LOGE(TAG, "Camera power is off, call bsp_camera_power_up() first");
    return ESP_ERR_INVALID_STATE;
  }

  camera_config_t config = {
      .pin_pwdn = -1,
      .pin_reset = -1,
      .pin_xclk = BSP_CAM_MCLK,
      .pin_sccb_sda = BSP_I2C_CAM_SDA,
      .pin_sccb_scl = BSP_I2C_CAM_SCL,
      .sccb_i2c_port = BSP_I2C_CAM_PORT,

      .pin_d7 = BSP_CAM_D7,
      .pin_d6 = BSP_CAM_D6,
      .pin_d5 = BSP_CAM_D5,
      .pin_d4 = BSP_CAM_D4,
      .pin_d3 = BSP_CAM_D3,
      .pin_d2 = BSP_CAM_D2,
      .pin_d1 = BSP_CAM_D1,
      .pin_d0 = BSP_CAM_D0,
      .pin_vsync = BSP_CAM_VSYNC,
      .pin_href = BSP_CAM_HSYNC,
      .pin_pclk = BSP_CAM_PCLK,

      .xclk_freq_hz = xclk_freq_hz,
      .ledc_timer = LEDC_TIMER_1,
      .ledc_channel = LEDC_CHANNEL_1,

      .pixel_format = pixel_format,
      .frame_size = frame_size,
      .jpeg_quality = 12,
      .fb_count = fb_count,
      .fb_location = CAMERA_FB_IN_PSRAM,
      .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
  };

  esp_err_t ret = esp_camera_init(&config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  /* 获取传感器信息 */
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    if (frame_size > s->status.framesize) {
      s->set_framesize(s, s->status.framesize);
    }
    ESP_LOGI(TAG, "Sensor PID=0x%04X, max framesize=%d", s->id.PID,
             s->status.framesize);
  }

  s_initialized = true;
  ESP_LOGI(TAG, "Camera initialized");
  return ESP_OK;
}

esp_err_t bsp_camera_deinit(void) {
  if (!s_initialized)
    return ESP_OK;
  esp_err_t ret = esp_camera_deinit();
  if (ret == ESP_OK)
    s_initialized = false;
  return ret;
}

/* ---- 帧操作 ---- */

camera_fb_t *bsp_camera_get_frame(void) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "Camera not initialized");
    return NULL;
  }
  return esp_camera_fb_get();
}

void bsp_camera_return_frame(camera_fb_t *fb) {
  if (fb)
    esp_camera_fb_return(fb);
}

const camera_sensor_info_t *bsp_camera_get_sensor_info(void) {
  sensor_t *s = esp_camera_sensor_get();
  if (!s)
    return NULL;
  return esp_camera_sensor_get_info(&s->id);
}
