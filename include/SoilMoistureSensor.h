#pragma once

#include <cstdint>
#include <optional>

#include <hardware/adc.h>
#include <hardware/gpio.h>

#include "Config.h"
#include "Types.h"

class SoilMoistureSensor final
{
public:
  explicit SoilMoistureSensor(uint8_t adcPin     = Config::SOIL_MOISTURE_ADC_PIN,
                              uint8_t adcChannel = Config::SOIL_MOISTURE_ADC_CHANNEL,
                              uint8_t powerPin   = Config::SOIL_MOISTURE_POWER_UP_PIN);
  ~SoilMoistureSensor() = default;

  SoilMoistureSensor(const SoilMoistureSensor&)                    = delete;
  auto operator=(const SoilMoistureSensor&) -> SoilMoistureSensor& = delete;
  SoilMoistureSensor(SoilMoistureSensor&&)                         = delete;
  auto operator=(SoilMoistureSensor&&) -> SoilMoistureSensor&      = delete;

  [[nodiscard]] auto init() -> bool;
  [[nodiscard]] auto read() -> std::optional<SoilMoistureData>;
  [[nodiscard]] auto isAvailable() const noexcept -> bool
  {
    return initialized_;
  }

  void calibrate(uint16_t dryValue, uint16_t wetValue) noexcept;

private:
  uint8_t adcPin_{};
  uint8_t adcChannel_{};
  uint8_t powerPin_{};
  bool    initialized_{false};

  uint16_t soilDryValue_{Config::SOIL_DRY_VALUE};
  uint16_t soilWetValue_{Config::SOIL_WET_VALUE};

  [[nodiscard]] static auto readADC(uint8_t channel) -> uint16_t;
  [[nodiscard]] static auto mapToPercentage(uint16_t value, uint16_t minVal, uint16_t maxVal) noexcept -> float;
};
