#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>

#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <pico/time.h>

#include "Config.h"
#include "SoilMoistureSensor.h"
#include "Types.h"

SoilMoistureSensor::SoilMoistureSensor(const uint8_t adcPin, const uint8_t adcChannel, const uint8_t powerPin)
  : adcPin_(adcPin), adcChannel_(adcChannel), powerPin_(powerPin)
{
}

auto SoilMoistureSensor::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  adc_init();
  adc_gpio_init(adcPin_);

  gpio_init(powerPin_);
  gpio_set_dir(powerPin_, GPIO_OUT);
  gpio_put(powerPin_, false);

  initialized_ = true;
  return initialized_;
}

auto SoilMoistureSensor::read() -> std::optional<SoilMoistureData>
{
  if (not initialized_ and not init()) [[unlikely]]
  {
    return std::nullopt;
  }

  SoilMoistureData data{};

  gpio_put(powerPin_, true);
  sleep_ms(Config::SOIL_MOISTURE_POWER_UP_MS);
  data.rawValue = readADC(adcChannel_);
  gpio_put(powerPin_, false);

  data.percentage = 100.0F - mapToPercentage(data.rawValue, soilWetValue_, soilDryValue_);
  data.percentage = std::clamp(data.percentage, 0.0F, 100.0F);
  data.valid      = true;

  return data;
}

void SoilMoistureSensor::calibrate(const uint16_t dryValue, const uint16_t wetValue) noexcept
{
  soilDryValue_ = dryValue;
  soilWetValue_ = wetValue;
  printf("[SoilMoistureSensor] Calibrated: dry=%u, wet=%u\n", dryValue, wetValue);
}

auto SoilMoistureSensor::readADC(const uint8_t channel) -> uint16_t
{
  adc_select_input(channel);
  return adc_read();
}

auto SoilMoistureSensor::mapToPercentage(const uint16_t value, const uint16_t minVal,
                                         const uint16_t maxVal) noexcept -> float
{
  if (maxVal <= minVal)
  {
    return 0.0F;
  }
  if (value <= minVal)
  {
    return 0.0F;
  }
  if (value >= maxVal)
  {
    return 100.0F;
  }
  return (static_cast<float>(value - minVal) / static_cast<float>(maxVal - minVal)) * 100.0F;
}
