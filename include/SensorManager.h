#pragma once

#include <memory>
#include <optional>

#include "BME280.h"
#include "Config.h"
#include "Types.h"

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

  [[nodiscard]] auto readBME280() -> BME280Data;
  [[nodiscard]] auto readSoilMoisture() const -> SoilMoistureData;
  [[nodiscard]] auto readWaterLevel() const -> WaterLevelData;

  void calibrateSoilMoisture(uint16_t dryValue, uint16_t wetValue) noexcept;
  void calibrateWaterLevel(uint16_t emptyValue, uint16_t fullValue) noexcept;

  [[nodiscard]] auto isInitialized() const noexcept -> bool
  {
    return initialized_;
  }
  [[nodiscard]] auto isBME280Available() const noexcept -> bool
  {
    return bme280_ and bme280_->isAvailable();
  }

private:
  bool                    initialized_{false};
  std::unique_ptr<BME280> bme280_;

  uint16_t soilDryValue_{Config::SOIL_DRY_VALUE};
  uint16_t soilWetValue_{Config::SOIL_WET_VALUE};
  uint16_t waterEmptyValue_{Config::WATER_EMPTY_THRESHOLD};
  uint16_t waterFullValue_{Config::WATER_FULL_THRESHOLD};

  [[nodiscard]] static auto readADC(uint8_t channel) -> uint16_t;
  [[nodiscard]] static auto mapToPercentage(uint16_t value, uint16_t minVal,
                                            uint16_t maxVal) noexcept -> float;
  static void               powerWaterSensor(bool enable);
};
