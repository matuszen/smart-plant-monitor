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

class SensorController final
{
public:
  SensorController()  = default;
  ~SensorController() = default;

  SensorController(const SensorController&)                    = delete;
  auto operator=(const SensorController&) -> SensorController& = delete;
  SensorController(SensorController&&)                         = delete;
  auto operator=(SensorController&&) -> SensorController&      = delete;

  [[nodiscard]] auto init() -> bool;

  [[nodiscard]] auto readAllSensors() -> SensorData;

  [[nodiscard]] auto readBME280() -> EnvironmentData;
  [[nodiscard]] auto readLightLevel() const -> LightLevelData;
  [[nodiscard]] auto readSoilMoisture() const -> SoilMoistureData;
  [[nodiscard]] auto readWaterLevel() const -> WaterLevelData;

  void calibrateSoilMoisture(uint16_t dryValue, uint16_t wetValue);

  [[nodiscard]] auto isInitialized() const -> bool;

private:
  bool                      initialized_ = false;
  mutable SemaphoreHandle_t sensorMutex_ = nullptr;

  std::unique_ptr<EnvironmentalSensor> environmentalSensor_;
  std::unique_ptr<LightSensor>         lightSensor_;
  std::unique_ptr<WaterLevelSensor>    waterSensor_;
  std::unique_ptr<SoilMoistureSensor>  soilSensor_;
};
