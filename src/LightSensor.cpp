#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>

#include <hardware/i2c.h>
#include <pico/time.h>

#include "LightSensor.h"
#include "Types.h"

namespace
{

inline constexpr float CORRECTION_FACTOR = 1.0F;

}  // namespace

LightSensor::LightSensor(i2c_inst_t* i2c, const uint8_t address) : i2c_(i2c), address_(address)
{
}

auto LightSensor::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  if (i2c_ == nullptr) [[unlikely]]
  {
    printf("[LightSensor] ERROR: Missing I2C instance\n");
    return false;
  }

  if (not writeCommand(POWER_ON_CMD)) [[unlikely]]
  {
    printf("[LightSensor] Power on failed\n");
    return false;
  }

  if (not writeCommand(CONT_HIGH_RES_MODE_CMD)) [[unlikely]]
  {
    printf("[LightSensor] Mode set failed\n");
    return false;
  }

  sleep_ms(200);
  initialized_ = true;
  return true;
}

auto LightSensor::read() -> std::optional<LightLevelData>
{
  if (not initialized_ and not init()) [[unlikely]]
  {
    return std::nullopt;
  }

  std::array<uint8_t, 2> buffer{};
  const auto             count = i2c_read_blocking(i2c_, address_, buffer.data(), buffer.size(), false);
  if (count != static_cast<int>(buffer.size())) [[unlikely]]
  {
    initialized_ = false;
    return std::nullopt;
  }

  const auto raw = static_cast<uint16_t>((buffer[0] << 8U) | buffer[1]);

  const auto data = LightLevelData{
    .rawValue = raw,
    .lux      = (static_cast<float>(raw) / 1.2F) * CORRECTION_FACTOR,
    .valid    = true,
  };
  return data;
}

auto LightSensor::writeCommand(const uint8_t command) -> bool
{
  return i2c_write_blocking(i2c_, address_, &command, 1, false) == 1;
}
