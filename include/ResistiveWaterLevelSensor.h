#pragma once

#include <cstdint>
#include <optional>

#include "Config.h"

class ResistiveWaterLevelSensor final
{
public:
  struct Reading
  {
    uint16_t rawValue{0};
  };

  ResistiveWaterLevelSensor(uint8_t adcPin     = Config::WATER_LEVEL_RESISTIVE_ADC_PIN,
                            uint8_t adcChannel = Config::WATER_LEVEL_RESISTIVE_ADC_CHANNEL);
  ~ResistiveWaterLevelSensor() = default;

  ResistiveWaterLevelSensor(const ResistiveWaterLevelSensor&)                        = delete;
  auto operator=(const ResistiveWaterLevelSensor&) -> ResistiveWaterLevelSensor&     = delete;
  ResistiveWaterLevelSensor(ResistiveWaterLevelSensor&&) noexcept                    = delete;
  auto operator=(ResistiveWaterLevelSensor&&) noexcept -> ResistiveWaterLevelSensor& = delete;

  [[nodiscard]] auto init() -> bool;
  [[nodiscard]] auto read() -> std::optional<Reading>;

  [[nodiscard]] auto isAvailable() const noexcept -> bool
  {
    return initialized_;
  }

private:
  uint8_t adcPin_;
  uint8_t adcChannel_;
  bool    initialized_{false};

  [[nodiscard]] auto sampleRaw() const -> uint16_t;
};
