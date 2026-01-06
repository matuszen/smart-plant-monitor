#include "SensorController.hpp"

#include "Common.hpp"
#include "Config.hpp"
#include "EnvironmentalSensor.hpp"
#include "LightSensor.hpp"
#include "SoilMoistureSensor.hpp"
#include "Types.hpp"
#include "WaterLevelSensor.hpp"

#include <FreeRTOS.h>
#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/structs/io_bank0.h>
#include <pico/time.h>
#include <portmacrocommon.h>
#include <projdefs.h>
#include <semphr.h>

#include <cstdint>
#include <cstdio>
#include <memory>

namespace
{

auto resolveI2CInstance(const uint8_t instance) -> i2c_inst_t*
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

auto SensorController::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  printf("[SensorController] Initializing...\n");

  sensorMutex_ = xSemaphoreCreateRecursiveMutex();
  if (sensorMutex_ == nullptr) [[unlikely]]
  {
    printf("[SensorController] ERROR: Failed to create sensor mutex\n");
    return false;
  }

  adc_init();

  auto* sharedI2C = resolveI2CInstance(Config::BME280_I2C_INSTANCE);
  auto* waterI2C  = resolveI2CInstance(Config::WATER_LEVEL_I2C_INSTANCE);

  if ((sharedI2C == nullptr) or (waterI2C == nullptr)) [[unlikely]]
  {
    printf("[SensorController] ERROR: Invalid I2C instance configuration\n");
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
  if (not environmentalSensor_->init()) [[unlikely]]
  {
    printf("[SensorController] WARNING: BME280 not detected\n");
    environmentalSensor_.reset();
  }
  lightSensor_ = std::make_unique<LightSensor>(sharedI2C, Config::LIGHT_SENSOR_I2C_ADDRESS);
  if (not lightSensor_->init()) [[unlikely]]
  {
    printf("[SensorController] WARNING: BH1750 not detected\n");
    lightSensor_.reset();
  }

  soilSensor_ = std::make_unique<SoilMoistureSensor>(Config::SOIL_MOISTURE_ADC_PIN, Config::SOIL_MOISTURE_ADC_CHANNEL,
                                                     Config::SOIL_MOISTURE_POWER_UP_PIN);
  if (not soilSensor_->init()) [[unlikely]]
  {
    printf("[SensorController] WARNING: Soil moisture sensor init failed\n");
    soilSensor_.reset();
  }

  waterSensor_ =
    std::make_unique<WaterLevelSensor>(waterI2C, Config::WATER_LEVEL_LOW_ADDR, Config::WATER_LEVEL_HIGH_ADDR);
  if (not waterSensor_->init()) [[unlikely]]
  {
    printf("[SensorController] WARNING: Water level sensor not detected\n");
    waterSensor_.reset();
  }

  initialized_ = true;
  printf("[SensorController] Initialization complete\n");
  return true;
}

auto SensorController::readAllSensors() const -> SensorData
{
  return {
    .environment = readBME280(),
    .light       = readLightLevel(),
    .soil        = readSoilMoisture(),
    .water       = readWaterLevel(),
    .timestamp   = Utils::getTimeSinceBoot(),
  };
}

auto SensorController::readBME280() const -> EnvironmentData
{
  EnvironmentData result{};

  if (xSemaphoreTakeRecursive(sensorMutex_, portMAX_DELAY) != pdPASS)
  {
    return result;
  }

  if (not environmentalSensor_)
  {
    xSemaphoreGiveRecursive(sensorMutex_);
    return result;
  }

  const auto measurement = environmentalSensor_->read();
  if (measurement)
  {
    result = measurement.value();
  }

  xSemaphoreGiveRecursive(sensorMutex_);
  return result;
}

auto SensorController::readLightLevel() const -> LightLevelData
{
  LightLevelData result{};

  if (xSemaphoreTakeRecursive(sensorMutex_, portMAX_DELAY) != pdPASS)
  {
    return result;
  }

  if (not lightSensor_)
  {
    xSemaphoreGiveRecursive(sensorMutex_);
    return result;
  }
  const auto measurement = lightSensor_->read();
  if (measurement)
  {
    result = measurement.value();
  }

  xSemaphoreGiveRecursive(sensorMutex_);
  return result;
}

auto SensorController::readSoilMoisture() const -> SoilMoistureData
{
  SoilMoistureData result{};

  if (xSemaphoreTakeRecursive(sensorMutex_, portMAX_DELAY) != pdPASS)
  {
    return result;
  }

  if (not soilSensor_)
  {
    xSemaphoreGiveRecursive(sensorMutex_);
    return result;
  }

  const auto measurement = soilSensor_->read();
  if (measurement)
  {
    result = measurement.value();
  }

  xSemaphoreGiveRecursive(sensorMutex_);
  return result;
}

auto SensorController::readWaterLevel() const -> WaterLevelData
{
  WaterLevelData result{};

  if (xSemaphoreTakeRecursive(sensorMutex_, portMAX_DELAY) != pdPASS)
  {
    return result;
  }

  if (not waterSensor_)
  {
    xSemaphoreGiveRecursive(sensorMutex_);
    return result;
  }

  const auto measurement = waterSensor_->read();
  if (measurement)
  {
    result = measurement.value();
  }

  xSemaphoreGiveRecursive(sensorMutex_);
  return result;
}

void SensorController::calibrateSoilMoisture(const uint16_t dryValue, const uint16_t wetValue)
{
  if (soilSensor_)
  {
    soilSensor_->calibrate(dryValue, wetValue);
  }
}

auto SensorController::isInitialized() const -> bool
{
  return initialized_;
}
