#include "bsp_pca9539.h"

#include "bsp_i2c.h"
#include "esp_log.h"

static const char *TAG = "PCA9539";

static uint8_t s_addr = BSP_PCA9539_ADDR;

/*-------------------- Registers --------------------*/

#define REG_INPUT0 0x00
#define REG_INPUT1 0x01

#define REG_OUTPUT0 0x02
#define REG_OUTPUT1 0x03

#define REG_POLARITY0 0x04
#define REG_POLARITY1 0x05

#define REG_CONFIG0 0x06
#define REG_CONFIG1 0x07

/*-------------------- Shadow Cache --------------------*/

static uint8_t s_output_cache[2];
static uint8_t s_config_cache[2];

/*----------------------------------------------------*/

static esp_err_t write_reg(uint8_t reg, uint8_t value) {
  uint8_t buf[2] = {reg, value};
  return bsp_i2c_write_main(s_addr, buf, 2, 1000);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *value) {
  return bsp_i2c_write_read_main(s_addr, &reg, 1, value, 1, 1000);
}

static inline uint8_t pin_to_port(uint8_t pin) { return pin >> 3; }

static inline uint8_t pin_to_bit(uint8_t pin) { return pin & 0x07; }

static inline uint8_t output_reg(uint8_t port) {
  return port ? REG_OUTPUT1 : REG_OUTPUT0;
}

static inline uint8_t input_reg(uint8_t port) {
  return port ? REG_INPUT1 : REG_INPUT0;
}

static inline uint8_t config_reg(uint8_t port) {
  return port ? REG_CONFIG1 : REG_CONFIG0;
}

/*----------------------------------------------------*/

esp_err_t bsp_pca9539_init(uint8_t addr) {
  s_addr = addr;

  s_output_cache[0] = 0;
  s_output_cache[1] = 0;

  s_config_cache[0] = 0;
  s_config_cache[1] = 0;

  // 尝试通信，增加 3 次重试（给休眠唤醒后的 I2C 状态机复位留出时间）
  esp_err_t ret = ESP_FAIL;
  for (int retry = 0; retry < 3; retry++) {
    ret = write_reg(REG_OUTPUT0, 0);
    if (ret == ESP_OK) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // 适当延时等待总线释放
  }

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Init Failed! PCA9539 not responding at 0x%02X", s_addr);
    return ret;
  }

  write_reg(REG_OUTPUT1, 0);
  write_reg(REG_CONFIG0, 0);
  write_reg(REG_CONFIG1, 0);

  ESP_LOGI(TAG, "Init OK (0x%02X)", s_addr);

  return ESP_OK;
}

/*----------------------------------------------------*/

esp_err_t bsp_pca9539_set_direction_port0(uint8_t value) {
  s_config_cache[0] = value;
  return write_reg(REG_CONFIG0, value);
}

esp_err_t bsp_pca9539_set_direction_port1(uint8_t value) {
  s_config_cache[1] = value;
  return write_reg(REG_CONFIG1, value);
}

esp_err_t bsp_pca9539_write_output_port0(uint8_t value) {
  s_output_cache[0] = value;
  return write_reg(REG_OUTPUT0, value);
}

esp_err_t bsp_pca9539_write_output_port1(uint8_t value) {
  s_output_cache[1] = value;
  return write_reg(REG_OUTPUT1, value);
}

esp_err_t bsp_pca9539_read_input_port0(uint8_t *value) {
  return read_reg(REG_INPUT0, value);
}

esp_err_t bsp_pca9539_read_input_port1(uint8_t *value) {
  return read_reg(REG_INPUT1, value);
}

/*----------------------------------------------------*/

esp_err_t bsp_pca9539_set_pin_direction(uint8_t pin, bool input) {
  if (pin > 15)
    return ESP_ERR_INVALID_ARG;

  uint8_t port = pin_to_port(pin);
  uint8_t bit = pin_to_bit(pin);

  if (input)
    s_config_cache[port] |= (1 << bit);
  else
    s_config_cache[port] &= ~(1 << bit);

  return write_reg(config_reg(port), s_config_cache[port]);
}

esp_err_t bsp_pca9539_set_pin_level(uint8_t pin, bool level) {
  if (pin > 15)
    return ESP_ERR_INVALID_ARG;

  uint8_t port = pin_to_port(pin);
  uint8_t bit = pin_to_bit(pin);

  if (level)
    s_output_cache[port] |= (1 << bit);
  else
    s_output_cache[port] &= ~(1 << bit);

  return write_reg(output_reg(port), s_output_cache[port]);
}

esp_err_t bsp_pca9539_get_pin_level(uint8_t pin, bool *level) {
  if (pin > 15 || level == NULL)
    return ESP_ERR_INVALID_ARG;

  uint8_t port = pin_to_port(pin);
  uint8_t bit = pin_to_bit(pin);

  uint8_t value;

  esp_err_t ret = read_reg(input_reg(port), &value);

  if (ret != ESP_OK)
    return ret;

  *level = (value >> bit) & 1;

  return ESP_OK;
}