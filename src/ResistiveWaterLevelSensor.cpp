#include "ResistiveWaterLevelSensor.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include <hardware/adc.h>

ResistiveWaterLevelSensor::ResistiveWaterLevelSensor(const uint8_t adcPin, const uint8_t adcChannel)
  : adcPin_(adcPin), adcChannel_(adcChannel)
{
}

auto ResistiveWaterLevelSensor::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  adc_gpio_init(adcPin_);

  initialized_ = true;
  return true;
}

auto ResistiveWaterLevelSensor::read() -> std::optional<Reading>
{
  if (not initialized_ and not init())
  {
    return std::nullopt;
  }

  const auto rawValue = sampleRaw();
  return Reading{rawValue};
}

auto ResistiveWaterLevelSensor::sampleRaw() const -> uint16_t
{
  const uint8_t samples = std::max<uint8_t>(1, Config::WATER_LEVEL_RESISTIVE_SAMPLES);
  uint32_t      total   = 0;

  adc_select_input(adcChannel_);

  for (uint8_t idx = 0; idx < samples; ++idx)
  {
    total += adc_read();
  }

  const uint32_t average = total / samples;
  return static_cast<uint16_t>(average & 0x0FFFU);
}
