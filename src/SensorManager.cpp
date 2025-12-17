#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>

#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <pico/time.h>

#include "Config.h"
#include "EnvironmentalSensor.h"
#include "LightSensor.h"
#include "SensorManager.h"
#include "Types.h"
#include "WaterLevelSensor.h"

namespace
{

[[nodiscard]] auto resolveI2CInstance(const uint8_t instance) -> i2c_inst_t*
{
  switch (instance)
  {
    case 0:
      return i2c0;
    case 1:
      return i2c1;
    default:
      return nullptr;
  }
}

}  // namespace

auto SensorManager::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  printf("[SensorManager] Initializing...\n");

  adc_init();
  adc_gpio_init(Config::SOIL_MOISTURE_ADC_PIN);
  gpio_init(Config::SOIL_MOISTURE_POWER_UP_PIN);
  gpio_set_dir(Config::SOIL_MOISTURE_POWER_UP_PIN, GPIO_OUT);
  gpio_put(Config::SOIL_MOISTURE_POWER_UP_PIN, false);

  auto* sharedI2C = resolveI2CInstance(Config::BME280_I2C_INSTANCE);
  auto* waterI2C  = resolveI2CInstance(Config::WATER_LEVEL_I2C_INSTANCE);

  if ((sharedI2C == nullptr) or (waterI2C == nullptr))
  {
    printf("[SensorManager] ERROR: Invalid I2C instance configuration\n");
    return false;
  }

  i2c_init(sharedI2C, Config::BME280_I2C_BAUDRATE);
  gpio_set_function(Config::BME280_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(Config::BME280_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(Config::BME280_SDA_PIN);
  gpio_pull_up(Config::BME280_SCL_PIN);

  i2c_init(waterI2C, Config::WATER_LEVEL_I2C_BAUDRATE);
  gpio_set_function(Config::WATER_LEVEL_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(Config::WATER_LEVEL_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(Config::WATER_LEVEL_SDA_PIN);
  gpio_pull_up(Config::WATER_LEVEL_SCL_PIN);

  gpio_init(Config::WATER_LEVEL_POWER_PIN);
  gpio_set_dir(Config::WATER_LEVEL_POWER_PIN, GPIO_OUT);
  gpio_put(Config::WATER_LEVEL_POWER_PIN, true);

  environmentalSensor_ = std::make_unique<EnvironmentalSensor>(sharedI2C, Config::BME280_I2C_ADDRESS);
  if (not environmentalSensor_->init())
  {
    printf("[SensorManager] WARNING: BME280 not detected\n");
    environmentalSensor_.reset();
  }

  lightSensor_ = std::make_unique<LightSensor>(sharedI2C, Config::LIGHT_SENSOR_I2C_ADDRESS);
  if (not lightSensor_->init())
  {
    printf("[SensorManager] WARNING: BH1750 not detected\n");
    lightSensor_.reset();
  }

  waterSensor_ =
    std::make_unique<WaterLevelSensor>(waterI2C, Config::WATER_LEVEL_LOW_ADDR, Config::WATER_LEVEL_HIGH_ADDR);
  if (not waterSensor_->init())
  {
    printf("[SensorManager] WARNING: Water level sensor not detected\n");
    waterSensor_.reset();
  }

  initialized_ = true;
  printf("[SensorManager] Initialization complete\n");
  return true;
}

auto SensorManager::readAllSensors() -> SensorData
{
  const auto data = SensorData{
    .environment = readBME280(),
    .light       = readLightLevel(),
    .soil        = readSoilMoisture(),
    .water       = readWaterLevel(),
    .timestamp   = to_ms_since_boot(get_absolute_time()),
  };
  return data;
}

auto SensorManager::readBME280() -> EnvironmentData
{
  if (not environmentalSensor_)
  {
    return EnvironmentData{};
  }

  const auto measurement = environmentalSensor_->read();
  if (not measurement)
  {
    return EnvironmentData{};
  }

  return measurement.value();
}

auto SensorManager::readLightLevel() const -> LightLevelData
{
  if (not lightSensor_)
  {
    return LightLevelData{};
  }

  const auto measurement = lightSensor_->read();
  if (not measurement)
  {
    return LightLevelData{};
  }

  return measurement.value();
}

auto SensorManager::readSoilMoisture() const -> SoilMoistureData
{
  SoilMoistureData data{};

  gpio_put(Config::SOIL_MOISTURE_POWER_UP_PIN, true);
  sleep_ms(Config::SOIL_MOISTURE_POWER_UP_MS);
  data.rawValue = readADC(Config::SOIL_MOISTURE_ADC_CHANNEL);
  gpio_put(Config::SOIL_MOISTURE_POWER_UP_PIN, false);
  data.percentage = 100.0F - mapToPercentage(data.rawValue, soilWetValue_, soilDryValue_);
  data.percentage = std::clamp(data.percentage, 0.0F, 100.0F);
  data.valid      = true;
  return data;
}

auto SensorManager::readWaterLevel() const -> WaterLevelData
{
  if (not waterSensor_)
  {
    return WaterLevelData{};
  }

  const auto measurement = waterSensor_->read();
  if (not measurement)
  {
    return WaterLevelData{};
  }

  return measurement.value();
}

void SensorManager::calibrateSoilMoisture(const uint16_t dryValue, const uint16_t wetValue) noexcept
{
  soilDryValue_ = dryValue;
  soilWetValue_ = wetValue;
  printf("[SensorManager] Soil moisture calibrated: dry=%u, wet=%u\n", dryValue, wetValue);
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
