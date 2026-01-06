#pragma once

#include "Config.hpp"
#include "Types.hpp"

#include <hardware/adc.h>
#include <hardware/gpio.h>

#include <cstdint>
#include <optional>

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

  auto init() -> bool;
  auto read() -> std::optional<SoilMoistureData>;
  auto isAvailable() const -> bool
  {
    return initialized_;
  }

  void calibrate(uint16_t dryValue, uint16_t wetValue);

private:
  uint8_t adcPin_      = 0;
  uint8_t adcChannel_  = 0;
  uint8_t powerPin_    = 0;
  bool    initialized_ = false;

  uint16_t soilDryValue_ = Config::SOIL_DRY_VALUE;
  uint16_t soilWetValue_ = Config::SOIL_WET_VALUE;

  static auto readADC(uint8_t channel) -> uint16_t;
  static auto mapToPercentage(uint16_t value, uint16_t minVal, uint16_t maxVal) -> float;
};
