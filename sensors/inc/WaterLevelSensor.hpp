#pragma once

#include "Config.hpp"
#include "Types.hpp"

#include <hardware/i2c.h>

#include <cstdint>
#include <optional>

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

  auto init() -> bool;
  auto read() -> std::optional<WaterLevelData>;
  auto isAvailable() const -> bool
  {
    return initialized_;
  }

private:
  static constexpr uint8_t LOW_SECTIONS   = 8;
  static constexpr uint8_t HIGH_SECTIONS  = 12;
  static constexpr uint8_t TOTAL_SECTIONS = LOW_SECTIONS + HIGH_SECTIONS;
  static_assert(TOTAL_SECTIONS == Config::WATER_LEVEL_TOTAL_SECTIONS, "Water level section count mismatch");

  i2c_inst_t* i2c_         = nullptr;
  uint8_t     lowAddress_  = 0;
  uint8_t     highAddress_ = 0;
  bool        initialized_ = false;
};
