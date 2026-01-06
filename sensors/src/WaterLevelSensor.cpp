#include "WaterLevelSensor.hpp"
#include "Config.hpp"
#include "Types.hpp"

#include <hardware/i2c.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>

WaterLevelSensor::WaterLevelSensor(i2c_inst_t* i2c, const uint8_t lowAddress, const uint8_t highAddress)
  : i2c_(i2c), lowAddress_(lowAddress), highAddress_(highAddress)
{
}

auto WaterLevelSensor::init() -> bool
{
  if (initialized_)
  {
    return true;
  }
  if (i2c_ == nullptr) [[unlikely]]
  {
    printf("[WaterLevelSensor] ERROR: Missing I2C instance\n");
    return false;
  }
  initialized_ = true;
  return true;
}

auto WaterLevelSensor::read() -> std::optional<WaterLevelData>
{
  if (not initialized_ and not init()) [[unlikely]]
  {
    return std::nullopt;
  }

  std::array<uint8_t, TOTAL_SECTIONS> sensorData{};

  const auto retLow  = i2c_read_blocking(i2c_, lowAddress_, sensorData.data(), LOW_SECTIONS, false);
  const auto retHigh = i2c_read_blocking(i2c_, highAddress_, &sensorData[LOW_SECTIONS], HIGH_SECTIONS, false);

  if ((retLow < 0) and (retHigh < 0)) [[unlikely]]
  {
    printf("[WaterLevelSensor] I2C error: sensor did not respond\n");
    return std::nullopt;
  }

  const auto activeSections = static_cast<uint8_t>(
    std::ranges::count_if(sensorData, [](const uint8_t v) -> bool { return v > Config::WATER_LEVEL_TOUCH_THRESHOLD; }));

  const auto waterLevelReading = WaterLevelData{
    .percentage     = std::min(100.0F, (activeSections / static_cast<float>(TOTAL_SECTIONS)) * 100.0F),
    .activeSections = static_cast<uint16_t>(activeSections * Config::WATER_LEVEL_SECTION_HEIGHT_MM),
    .valid          = true,
  };
  return waterLevelReading;
}
