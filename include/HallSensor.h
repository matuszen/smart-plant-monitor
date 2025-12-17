#pragma once

#include <cstdint>
#include <optional>

#include <hardware/gpio.h>

#include "Config.h"
#include "Types.h"

class HallSensor final
{
public:
  explicit HallSensor(uint8_t powerPin = Config::HALL_POWER_PIN, uint8_t signalPin = Config::HALL_SIGNAL_PIN);
  ~HallSensor() = default;

  HallSensor(const HallSensor&)                    = delete;
  auto operator=(const HallSensor&) -> HallSensor& = delete;
  HallSensor(HallSensor&&)                         = delete;
  auto operator=(HallSensor&&) -> HallSensor&      = delete;

  [[nodiscard]] auto init() -> bool;
  [[nodiscard]] auto read() -> std::optional<HallSensorData>;
  [[nodiscard]] auto isAvailable() const noexcept -> bool
  {
    return initialized_;
  }

private:
  uint8_t powerPin_{};
  uint8_t signalPin_{};
  bool    initialized_{false};
};
