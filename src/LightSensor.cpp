#include "LightSensor.h"

#include <cstdio>

#include <hardware/i2c.h>
#include <pico/time.h>

#include "Config.h"

LightSensor::LightSensor(i2c_inst_t* i2c, const uint8_t address) : i2c_(i2c), address_(address)
{
}

auto LightSensor::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  if (i2c_ == nullptr)
  {
    printf("[LightSensor] ERROR: Missing I2C instance\n");
    return false;
  }

  printf("[LightSensor] Initializing BH1750 at 0x%02X...\n", address_);

  if (not writeCommand(POWER_ON_CMD))
  {
    printf("[LightSensor] ERROR: Power on command failed\n");
    return false;
  }

  // Datasheet recommends RESET after power on.
  if (not writeCommand(RESET_CMD))
  {
    printf("[LightSensor] WARNING: Reset command failed, continuing\n");
  }

  if (not writeCommand(CONT_HIGH_RES_MODE_CMD))
  {
    printf("[LightSensor] ERROR: Failed to enter continuous high-res mode\n");
    return false;
  }

  sleep_ms(200);  // allow first measurement to complete (max 180ms)

  initialized_      = true;
  readFailedLogged_ = false;
  printf("[LightSensor] Initialized successfully\n");
  return true;
}

auto LightSensor::read() -> std::optional<Reading>
{
  if (not initialized_ and not init())
  {
    return std::nullopt;
  }

  uint8_t   buffer[2]{};
  const int count = i2c_read_blocking(i2c_, address_, buffer, 2, false);
  if (count != 2)
  {
    if (not readFailedLogged_)
    {
      printf("[LightSensor] WARNING: I2C read failed (got %d)\n", count);
      readFailedLogged_ = true;
    }
    initialized_ = false;
    return std::nullopt;
  }

  readFailedLogged_ = false;

  const uint16_t raw     = static_cast<uint16_t>((static_cast<uint16_t>(buffer[0]) << 8U) | buffer[1]);
  auto           reading = Reading{};
  reading.raw            = raw;
  reading.lux            = static_cast<float>(raw) / 1.2F;  // datasheet: divide by 1.2 for lux

  return reading;
}

auto LightSensor::writeCommand(const uint8_t command) -> bool
{
  const int written = i2c_write_blocking(i2c_, address_, &command, 1, false);
  return written == 1;
}
