#include <cstdint>
#include <cstdio>
#include <optional>

#include <hardware/gpio.h>
#include <pico/stdlib.h>

#include "HallSensor.h"
#include "Types.h"

HallSensor::HallSensor(const uint8_t powerPin, const uint8_t signalPin) : powerPin_(powerPin), signalPin_(signalPin)
{
}

auto HallSensor::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  gpio_init(powerPin_);
  gpio_set_dir(powerPin_, GPIO_OUT);
  gpio_put(powerPin_, true);

  gpio_init(signalPin_);
  gpio_set_dir(signalPin_, GPIO_IN);
  gpio_pull_up(signalPin_);

  initialized_ = true;
  printf("[HallSensor] Initialized (power=%u, signal=%u)\n", powerPin_, signalPin_);
  return initialized_;
}

auto HallSensor::read() -> std::optional<HallSensorData>
{
  if (not initialized_ and not init())
  {
    return std::nullopt;
  }

  const auto detected = (gpio_get(signalPin_) == 0);

  const auto data = HallSensorData{
    .magnetDetected = detected,
    .valid          = true,
  };
  return data;
}
