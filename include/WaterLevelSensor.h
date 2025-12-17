#pragma once

#include <cstdint>
#include <optional>

#include <hardware/i2c.h>

#include "Config.h"
#include "Types.h"

class WaterLevelSensor final
{
public:
  explicit WaterLevelSensor(i2c_inst_t* i2c, uint8_t lowAddress = Config::WATER_LEVEL_LOW_ADDR,
                            uint8_t highAddress = Config::WATER_LEVEL_HIGH_ADDR);
  ~WaterLevelSensor() = default;

  WaterLevelSensor(const WaterLevelSensor&)                    = delete;
  auto operator=(const WaterLevelSensor&) -> WaterLevelSensor& = delete;
  WaterLevelSensor(WaterLevelSensor&&)                         = delete;
  auto operator=(WaterLevelSensor&&) -> WaterLevelSensor&      = delete;

  [[nodiscard]] auto init() -> bool;
  [[nodiscard]] auto read() -> std::optional<WaterLevelData>;
  [[nodiscard]] auto isAvailable() const noexcept -> bool
  {
    return initialized_;
  }

private:
  static constexpr uint8_t LOW_SECTIONS{8};
  static constexpr uint8_t HIGH_SECTIONS{12};
  static constexpr uint8_t TOTAL_SECTIONS{LOW_SECTIONS + HIGH_SECTIONS};
  static_assert(TOTAL_SECTIONS == Config::WATER_LEVEL_TOTAL_SECTIONS, "Water level section count mismatch");

  i2c_inst_t* i2c_{};
  uint8_t     lowAddress_{};
  uint8_t     highAddress_{};
  bool        initialized_{false};
};
