#pragma once

#include <cstdint>
#include <memory>

#include <FreeRTOS.h>
#include <semphr.h>

#include <hardware/i2c.h>

#include "EnvironmentalSensor.h"
#include "HallSensor.h"
#include "LightSensor.h"
#include "SoilMoistureSensor.h"
#include "Types.h"
#include "WaterLevelSensor.h"

class SensorManager final
{
public:
  SensorManager()  = default;
  ~SensorManager() = default;

  SensorManager(const SensorManager&)                        = delete;
  auto operator=(const SensorManager&) -> SensorManager&     = delete;
  SensorManager(SensorManager&&) noexcept                    = delete;
  auto operator=(SensorManager&&) noexcept -> SensorManager& = delete;

  [[nodiscard]] auto init() -> bool;

  [[nodiscard]] auto readAllSensors() -> SensorData;

  [[nodiscard]] auto readBME280() -> EnvironmentData;
  [[nodiscard]] auto readLightLevel() const -> LightLevelData;
  [[nodiscard]] auto readSoilMoisture() const -> SoilMoistureData;
  [[nodiscard]] auto readHallSensor() const -> HallSensorData;
  [[nodiscard]] auto readWaterLevel() const -> WaterLevelData;

  void calibrateSoilMoisture(uint16_t dryValue, uint16_t wetValue) noexcept;

  [[nodiscard]] auto isInitialized() const noexcept -> bool
  {
    return initialized_;
  }
  [[nodiscard]] auto isBME280Available() const noexcept -> bool
  {
    return environmentalSensor_ and environmentalSensor_->isAvailable();
  }

private:
  bool                                 initialized_{false};
  std::unique_ptr<EnvironmentalSensor> environmentalSensor_;
  std::unique_ptr<LightSensor>         lightSensor_;
  std::unique_ptr<WaterLevelSensor>    waterSensor_;
  std::unique_ptr<SoilMoistureSensor>  soilSensor_;
  std::unique_ptr<HallSensor>          hallSensor_;
  mutable SemaphoreHandle_t            sensorMutex_{nullptr};
};
