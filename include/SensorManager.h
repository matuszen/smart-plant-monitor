#pragma once

#include <cstdint>
#include <memory>

#include <hardware/i2c.h>

#include "Config.h"
#include "EnvironmentalSensor.h"
#include "LightSensor.h"
#include "Types.h"
#include "WaterLevelSensor.h"

class SensorManager final
{
public:
  SensorManager()  = default;
  ~SensorManager() = default;

  SensorManager(const SensorManager&)                        = delete;
  auto operator=(const SensorManager&) -> SensorManager&     = delete;
  SensorManager(SensorManager&&) noexcept                    = default;
  auto operator=(SensorManager&&) noexcept -> SensorManager& = default;

  [[nodiscard]] auto init() -> bool;

  [[nodiscard]] auto readAllSensors() -> SensorData;

  [[nodiscard]] auto readBME280() -> EnvironmentData;
  [[nodiscard]] auto readLightLevel() const -> LightLevelData;
  [[nodiscard]] auto readSoilMoisture() const -> SoilMoistureData;
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

  uint16_t soilDryValue_{Config::SOIL_DRY_VALUE};
  uint16_t soilWetValue_{Config::SOIL_WET_VALUE};

  [[nodiscard]] static auto readADC(uint8_t channel) -> uint16_t;
  [[nodiscard]] static auto mapToPercentage(uint16_t value, uint16_t minVal, uint16_t maxVal) noexcept -> float;
};
