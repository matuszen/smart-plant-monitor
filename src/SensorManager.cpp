#include <algorithm>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "SensorManager.h"

auto SensorManager::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  printf("[SensorManager] Initializing...\n");

  adc_init();
  adc_gpio_init(Config::SOIL_MOISTURE_PIN);
  adc_gpio_init(Config::WATER_LEVEL_SIGNAL_PIN);

  gpio_init(Config::WATER_LEVEL_POWER_PIN);
  gpio_set_dir(Config::WATER_LEVEL_POWER_PIN, false);
  gpio_put(Config::WATER_LEVEL_POWER_PIN, false);

  i2c_init(i2c0, Config::BME280_I2C_BAUDRATE);
  gpio_set_function(Config::BME280_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(Config::BME280_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(Config::BME280_SDA_PIN);
  gpio_pull_up(Config::BME280_SCL_PIN);

  bme280_ = std::make_unique<BME280>(i2c0, Config::BME280_I2C_ADDRESS);
  if (bme280_->init())
  {
    printf("[SensorManager] BME280 initialized successfully\n");
  }
  else
  {
    printf("[SensorManager] WARNING: BME280 not found or failed to initialize\n");
    bme280_.reset();
  }

  initialized_ = true;
  printf("[SensorManager] Initialization complete\n");
  return true;
}

auto SensorManager::readAllSensors() -> SensorData
{
  auto data        = SensorData{};
  data.timestamp   = to_ms_since_boot(get_absolute_time());
  data.environment = readBME280();
  data.soil        = readSoilMoisture();
  data.water       = readWaterLevel();

  return data;
}

auto SensorManager::readBME280() -> BME280Data
{
  auto data = BME280Data{};

  if (not bme280_)
  {
    return data;
  }

  const auto measurement = bme280_->read();
  if (measurement)
  {
    data.temperature = measurement->temperature;
    data.humidity    = measurement->humidity;
    data.pressure    = measurement->pressure;
    data.valid       = true;
  }

  return data;
}

auto SensorManager::readSoilMoisture() const -> SoilMoistureData
{
  auto data       = SoilMoistureData{};
  data.rawValue   = readADC(Config::SOIL_MOISTURE_ADC);
  data.percentage = 100.0F - mapToPercentage(data.rawValue, soilWetValue_, soilDryValue_);
  data.percentage = std::clamp(data.percentage, 0.0F, 100.0F);
  data.valid      = true;

  return data;
}

auto SensorManager::readWaterLevel() const -> WaterLevelData
{
  auto data = WaterLevelData{};

  powerWaterSensor(true);
  sleep_ms(Config::WATER_SENSOR_POWER_DELAY_MS);

  data.rawValue = readADC(Config::WATER_LEVEL_ADC);

  powerWaterSensor(false);

  data.percentage = mapToPercentage(data.rawValue, waterEmptyValue_, waterFullValue_);
  data.percentage = std::clamp(data.percentage, 0.0F, 100.0F);
  data.valid      = true;

  return data;
}

void SensorManager::calibrateSoilMoisture(const uint16_t dryValue, const uint16_t wetValue) noexcept
{
  soilDryValue_ = dryValue;
  soilWetValue_ = wetValue;
  printf("[SensorManager] Soil moisture calibrated: dry=%u, wet=%u\n", dryValue, wetValue);
}

void SensorManager::calibrateWaterLevel(const uint16_t emptyValue,
                                        const uint16_t fullValue) noexcept
{
  waterEmptyValue_ = emptyValue;
  waterFullValue_  = fullValue;
  printf("[SensorManager] Water level calibrated: empty=%u, full=%u\n", emptyValue, fullValue);
}

auto SensorManager::readADC(const uint8_t channel) -> uint16_t
{
  adc_select_input(channel);
  return adc_read();
}

auto SensorManager::mapToPercentage(const uint16_t value, const uint16_t minVal,
                                    const uint16_t maxVal) noexcept -> float
{
  if (maxVal <= minVal)
  {
    return 0.0F;
  }

  if (value <= minVal)
  {
    return 0.0F;
  }

  if (value >= maxVal)
  {
    return 100.0F;
  }

  return (static_cast<float>(value - minVal) / static_cast<float>(maxVal - minVal)) * 100.0F;
}

void SensorManager::powerWaterSensor(const bool enable)
{
  gpio_put(Config::WATER_LEVEL_POWER_PIN, enable);
}
