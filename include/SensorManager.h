#pragma once

#include <cstdint>
#include <memory>

#include <hardware/i2c.h>

#include "BME280.h"
#include "Config.h"
#include "ResistiveWaterLevelSensor.h"
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

  [[nodiscard]] auto readBME280() -> BME280Data;
  [[nodiscard]] auto readSoilMoisture() const -> SoilMoistureData;
  [[nodiscard]] auto readWaterLevel() const -> WaterLevelData;
  [[nodiscard]] auto readResistiveWaterLevel() const -> ResistiveWaterData;

  void calibrateSoilMoisture(uint16_t dryValue, uint16_t wetValue) noexcept;

  [[nodiscard]] auto isInitialized() const noexcept -> bool
  {
    return initialized_;
  }
  [[nodiscard]] auto isBME280Available() const noexcept -> bool
  {
    return bme280_ and bme280_->isAvailable();
  }

private:
  bool                                       initialized_{false};
  std::unique_ptr<BME280>                    bme280_;
  std::unique_ptr<WaterLevelSensor>          waterSensor_;
  std::unique_ptr<ResistiveWaterLevelSensor> resistiveWaterSensor_;
  mutable bool                               waterSensorMissingLogged_{false};
  mutable bool                               waterSensorReadFailedLogged_{false};
  mutable ResistiveWaterData                 cachedResistiveWaterData_{};
  mutable uint32_t                           lastResistiveWaterReadMs_{0};

  uint16_t soilDryValue_{Config::SOIL_DRY_VALUE};
  uint16_t soilWetValue_{Config::SOIL_WET_VALUE};

  [[nodiscard]] static auto readADC(uint8_t channel) -> uint16_t;
  [[nodiscard]] static auto mapToPercentage(uint16_t value, uint16_t minVal,
                                            uint16_t maxVal) noexcept -> float;
  static void               scanI2CBus(i2c_inst_t* bus, uint8_t instanceId);
};
