#pragma once

#include "EnvironmentalSensor.hpp"
#include "LightSensor.hpp"
#include "SoilMoistureSensor.hpp"
#include "Types.hpp"
#include "WaterLevelSensor.hpp"

#include <FreeRTOS.h>
#include <hardware/i2c.h>
#include <semphr.h>

#include <cstdint>
#include <memory>

class SensorManager final
{
public:
  SensorManager()  = default;
  ~SensorManager() = default;

  SensorManager(const SensorManager&)                    = delete;
  auto operator=(const SensorManager&) -> SensorManager& = delete;
  SensorManager(SensorManager&&)                         = delete;
  auto operator=(SensorManager&&) -> SensorManager&      = delete;

  [[nodiscard]] auto init() -> bool;

  [[nodiscard]] auto readAllSensors() -> SensorData;

  [[nodiscard]] auto readBME280() -> EnvironmentData;
  [[nodiscard]] auto readLightLevel() const -> LightLevelData;
  [[nodiscard]] auto readSoilMoisture() const -> SoilMoistureData;
  [[nodiscard]] auto readWaterLevel() const -> WaterLevelData;

  void calibrateSoilMoisture(uint16_t dryValue, uint16_t wetValue);

  [[nodiscard]] auto isInitialized() const -> bool
  {
    return initialized_;
  }
  [[nodiscard]] auto isBME280Available() const -> bool
  {
    return environmentalSensor_ and environmentalSensor_->isAvailable();
  }

private:
  bool                                 initialized_{false};
  std::unique_ptr<EnvironmentalSensor> environmentalSensor_;
  std::unique_ptr<LightSensor>         lightSensor_;
  std::unique_ptr<WaterLevelSensor>    waterSensor_;
  std::unique_ptr<SoilMoistureSensor>  soilSensor_;
  mutable SemaphoreHandle_t            sensorMutex_{nullptr};
};
