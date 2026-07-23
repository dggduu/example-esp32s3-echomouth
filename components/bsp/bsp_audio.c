#include "bsp_audio.h"
#include "bsp_config.h"
#include "bsp_i2c.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

static const char *TAG = "BSP_AUDIO";
static esp_codec_dev_handle_t s_play_dev = NULL;   // 播放设备（ES8311）
static esp_codec_dev_handle_t s_record_dev = NULL; // 录音设备（ES7210）

esp_err_t bsp_audio_init(void *tx_handle, void *rx_handle) {
  if (s_play_dev != NULL || s_record_dev != NULL) {
    ESP_LOGW(TAG, "Audio already initialized");
    return ESP_OK;
  }

  // 1. 获取主 I2C 总线句柄（新版）
  i2c_master_bus_handle_t i2c_bus = bsp_i2c_get_main_handle();
  if (i2c_bus == NULL) {
    ESP_LOGE(TAG, "Main I2C bus not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // 2. 创建 I2S 数据接口（播放和录音共用）
  audio_codec_i2s_cfg_t i2s_cfg = {
      .port = BSP_I2S_PORT,
      .rx_handle = rx_handle, // 录音
      .tx_handle = tx_handle, // 播放
                              // .clk_src = 0,           // 默认时钟源
  };
  const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
  if (data_if == NULL) {
    ESP_LOGE(TAG, "Failed to create I2S data interface");
    return ESP_ERR_NO_MEM;
  }

  // 3. 创建 GPIO 接口（用于 PA 控制，但我们使用 PCA9539，此处不使用）
  const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
  if (gpio_if == NULL) {
    ESP_LOGE(TAG, "Failed to create GPIO interface");
    return ESP_ERR_NO_MEM;
  }

  // 4. 创建 ES8311 控制接口（播放）
  audio_codec_i2c_cfg_t i2c_cfg_8311 = {
      .addr = BSP_ES8311_ADDR,
      .bus_handle = i2c_bus,
  };
  const audio_codec_ctrl_if_t *ctrl_if_8311 =
      audio_codec_new_i2c_ctrl(&i2c_cfg_8311);
  if (ctrl_if_8311 == NULL) {
    ESP_LOGE(TAG, "Failed to create I2C control for ES8311");
    return ESP_ERR_NO_MEM;
  }

  // 5. 创建 ES8311 编解码器接口（播放）
  es8311_codec_cfg_t es8311_cfg = {
      .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC, // 只播放
      .ctrl_if = ctrl_if_8311,
      .gpio_if = gpio_if,
      .pa_pin = -1, // PA 由 PCA9539 控制
      .use_mclk = false,
  };
  const audio_codec_if_t *out_codec_if = es8311_codec_new(&es8311_cfg);
  if (out_codec_if == NULL) {
    ESP_LOGE(TAG, "Failed to create ES8311 codec interface");
    return ESP_ERR_NO_MEM;
  }

  // 6. 创建 ES7210 控制接口（录音）
  audio_codec_i2c_cfg_t i2c_cfg_7210 = {
      .addr = BSP_ES7210_ADDR,
      .bus_handle = i2c_bus,
  };
  const audio_codec_ctrl_if_t *ctrl_if_7210 =
      audio_codec_new_i2c_ctrl(&i2c_cfg_7210);
  if (ctrl_if_7210 == NULL) {
    ESP_LOGE(TAG, "Failed to create I2C control for ES7210");
    return ESP_ERR_NO_MEM;
  }

  // 7. 创建 ES7210 编解码器接口（录音）
  es7210_codec_cfg_t es7210_cfg = {
      .ctrl_if = ctrl_if_7210,
      .mic_selected =
          ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3, // 根据硬件选择
  };
  const audio_codec_if_t *in_codec_if = es7210_codec_new(&es7210_cfg);
  if (in_codec_if == NULL) {
    ESP_LOGE(TAG, "Failed to create ES7210 codec interface");
    return ESP_ERR_NO_MEM;
  }

  // 8. 创建播放设备句柄
  esp_codec_dev_cfg_t dev_cfg = {
      .codec_if = out_codec_if,
      .data_if = data_if,
      .dev_type = ESP_CODEC_DEV_TYPE_OUT,
  };
  s_play_dev = esp_codec_dev_new(&dev_cfg);
  if (s_play_dev == NULL) {
    ESP_LOGE(TAG, "Failed to create play device");
    return ESP_ERR_NO_MEM;
  }

  // 9. 创建录音设备句柄
  dev_cfg.codec_if = in_codec_if;
  dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
  s_record_dev = esp_codec_dev_new(&dev_cfg);
  if (s_record_dev == NULL) {
    ESP_LOGE(TAG, "Failed to create record device");
    esp_codec_dev_delete(s_play_dev);
    s_play_dev = NULL;
    return ESP_ERR_NO_MEM;
  }

  // 10. 设置默认采样参数
  esp_codec_dev_sample_info_t fs = {
      .bits_per_sample = 16,
      .channel = 2,
      .channel_mask = 0,
      .sample_rate = 16000,
      .mclk_multiple = 0,
  };

  // 打开播放设备
  int ret = esp_codec_dev_open(s_play_dev, &fs);
  if (ret != ESP_CODEC_DEV_OK) {
    ESP_LOGE(TAG, "Failed to open play device: %d", ret);
    esp_codec_dev_delete(s_play_dev);
    esp_codec_dev_delete(s_record_dev);
    s_play_dev = s_record_dev = NULL;
    return ESP_FAIL;
  }

  // 打开录音设备
  ret = esp_codec_dev_open(s_record_dev, &fs);
  if (ret != ESP_CODEC_DEV_OK) {
    ESP_LOGE(TAG, "Failed to open record device: %d", ret);
    esp_codec_dev_close(s_play_dev);
    esp_codec_dev_delete(s_play_dev);
    esp_codec_dev_delete(s_record_dev);
    s_play_dev = s_record_dev = NULL;
    return ESP_FAIL;
  }

  // 设置默认音量/增益
  esp_codec_dev_set_out_vol(s_play_dev, 60);
  esp_codec_dev_set_in_gain(s_record_dev, 37.5f);

  ESP_LOGI(TAG, "Audio initialized: ES8311 play, ES7210 record");
  return ESP_OK;
}

esp_err_t bsp_audio_set_volume(int volume) {
  if (s_play_dev == NULL)
    return ESP_ERR_INVALID_STATE;
  if (volume < 0 || volume > 100)
    return ESP_ERR_INVALID_ARG;
  return (esp_err_t)esp_codec_dev_set_out_vol(s_play_dev, volume);
}

esp_err_t bsp_audio_set_mic_gain(float gain) {
  if (s_record_dev == NULL)
    return ESP_ERR_INVALID_STATE;
  return (esp_err_t)esp_codec_dev_set_in_gain(s_record_dev, gain);
}

esp_err_t bsp_audio_play(const uint8_t *data, int len) {
  if (s_play_dev == NULL)
    return ESP_ERR_INVALID_STATE;
  return (esp_err_t)esp_codec_dev_write(s_play_dev, (void *)data, len);
}

esp_err_t bsp_audio_record(uint8_t *buf, int len) {
  if (s_record_dev == NULL)
    return ESP_ERR_INVALID_STATE;
  return (esp_err_t)esp_codec_dev_read(s_record_dev, buf, len);
}

esp_err_t bsp_audio_deinit(void) {
  if (s_play_dev) {
    esp_codec_dev_close(s_play_dev);
    esp_codec_dev_delete(s_play_dev);
    s_play_dev = NULL;
  }
  if (s_record_dev) {
    esp_codec_dev_close(s_record_dev);
    esp_codec_dev_delete(s_record_dev);
    s_record_dev = NULL;
  }
  return ESP_OK;
}