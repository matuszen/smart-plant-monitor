#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <optional>
#include <span>

#include <hardware/i2c.h>
#include <pico/time.h>

#include "Config.h"
#include "WaterLevelSensor.h"

namespace
{

void dumpBuffer(const char* label, std::span<const uint8_t> data)
{
  if constexpr (not Config::ENABLE_SERIAL_DEBUG)
  {
    return;
  }

  printf("[WaterLevelSensor] %s (%zu)", label, data.size());
  for (const auto value : data)
  {
    printf(" %3u", value);
  }
  printf("\n");
}

auto hasSignal(std::span<const uint8_t> data) -> bool
{
  return std::ranges::any_of(data, [](const uint8_t value) -> bool
                             { return value >= Config::WATER_LEVEL_WAKE_MIN_SIGNAL; });
}

}  // namespace

WaterLevelSensor::WaterLevelSensor(i2c_inst_t* i2c, const uint8_t lowAddress,
                                   const uint8_t highAddress)
  : i2c_(i2c), lowAddress_(lowAddress), highAddress_(highAddress), activeAddress_(lowAddress)
{
}

auto WaterLevelSensor::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  if (i2c_ == nullptr)
  {
    printf("[WaterLevelSensor] ERROR: Missing I2C instance\n");
    return false;
  }

  printf("[WaterLevelSensor] Probing sensor (low=0x%02X, high=0x%02X)\n", lowAddress_,
         highAddress_);

  std::array<uint8_t, TOTAL_SECTIONS> buffer{};

  if (readSplit(buffer))
  {
    combinedMode_ = false;
    dumpBuffer("Init split data", std::span<const uint8_t>{buffer});
  }
  else if (readCombined(lowAddress_, buffer))
  {
    combinedMode_  = true;
    activeAddress_ = lowAddress_;
    dumpBuffer("Init combined data", std::span<const uint8_t>{buffer});
  }
  else if ((lowAddress_ != highAddress_) and readCombined(highAddress_, buffer))
  {
    combinedMode_  = true;
    activeAddress_ = highAddress_;
    dumpBuffer("Init combined data", std::span<const uint8_t>{buffer});
  }
  else
  {
    printf("[WaterLevelSensor] ERROR: Unable to communicate with sensor (addr 0x%02X/0x%02X)\n",
           lowAddress_, highAddress_);
    return false;
  }

  initialized_             = true;
  startupPrimed_           = false;
  warmupAttemptsRemaining_ = Config::WATER_LEVEL_WAKE_RETRIES;
  printf("[WaterLevelSensor] Initialized successfully\n");
  return true;
}

auto WaterLevelSensor::read() -> std::optional<Reading>
{
  if (not initialized_ and not init())
  {
    return std::nullopt;
  }

  std::array<uint8_t, TOTAL_SECTIONS> buffer{};

  if (not fetchSensorBuffer(buffer))
  {
    printf("[WaterLevelSensor] WARNING: Read failed\n");
    initialized_ = false;
    return std::nullopt;
  }

  warmupIfStuck(buffer);

  std::array<uint8_t, LOW_SECTIONS>  low{};
  std::array<uint8_t, HIGH_SECTIONS> high{};
  std::copy_n(buffer.begin(), LOW_SECTIONS, low.begin());
  std::copy_n(std::next(buffer.begin(), LOW_SECTIONS), HIGH_SECTIONS, high.begin());

  const auto sensorMap  = buildSensorMap(low, high);
  const auto sections   = countContinuousSections(sensorMap);
  const auto activePads = static_cast<uint8_t>(
    std::ranges::count_if(buffer.begin(), buffer.end(), [](const uint8_t value) -> bool
                          { return value > Config::WATER_LEVEL_TOUCH_THRESHOLD; }));

  auto reading       = Reading{};
  reading.sections   = sections;
  reading.depthMm    = static_cast<uint16_t>(sections * Config::WATER_LEVEL_SECTION_HEIGHT_MM);
  reading.percentage = std::min(100.0F, (sections / static_cast<float>(TOTAL_SECTIONS)) * 100.0F);

  if constexpr (Config::ENABLE_SERIAL_DEBUG)
  {
    const float levelCm  = (activePads * Config::WATER_LEVEL_SECTION_HEIGHT_MM) / 10.0F;
    const float levelPct = (activePads / static_cast<float>(TOTAL_SECTIONS)) * 100.0F;
    printf(
      "[WaterLevelSensor] Map=0x%05lX, contiguous=%u, active=%u, depth=%umm, %.1f cm (%.0f%%)\n",
      static_cast<unsigned long>(sensorMap), sections, activePads, reading.depthMm, levelCm,
      levelPct);
  }

  return reading;
}

auto WaterLevelSensor::readBlock(const uint8_t address, uint8_t* buffer,
                                 const size_t length) -> bool
{
  if ((i2c_ == nullptr) or (buffer == nullptr) or (length == 0))
  {
    printf("[WaterLevelSensor] readBlock guard tripped (i2c=%p, buffer=%p, len=%zu)\n",
           static_cast<void*>(i2c_), static_cast<void*>(buffer), length);
    return false;
  }

  if constexpr (Config::ENABLE_SERIAL_DEBUG)
  {
    printf("[WaterLevelSensor] readBlock addr=0x%02X len=%zu\n", address, length);
  }

  const auto result = i2c_read_blocking(i2c_, address, buffer, length, false);
  if (result != static_cast<int>(length))
  {
    printf("[WaterLevelSensor] readBlock failed (addr=0x%02X, expected=%zu, got=%d)\n", address,
           length, result);
    return false;
  }

  if (Config::WATER_LEVEL_READ_DELAY_MS > 0U)
  {
    sleep_ms(Config::WATER_LEVEL_READ_DELAY_MS);
  }
  return true;
}

auto WaterLevelSensor::readCombined(const uint8_t                        address,
                                    std::array<uint8_t, TOTAL_SECTIONS>& buffer) -> bool
{
  if (readBlock(address, buffer.data(), buffer.size()))
  {
    combinedMode_  = true;
    activeAddress_ = address;
    return true;
  }
  return false;
}

auto WaterLevelSensor::readSplit(std::array<uint8_t, TOTAL_SECTIONS>& buffer) -> bool
{
  std::array<uint8_t, LOW_SECTIONS>  low{};
  std::array<uint8_t, HIGH_SECTIONS> high{};

  if (not readBlock(lowAddress_, low.data(), low.size()) or
      not readBlock(highAddress_, high.data(), high.size()))
  {
    return false;
  }

  std::ranges::copy(low, buffer.begin());
  std::ranges::copy(high, std::next(buffer.begin(), LOW_SECTIONS));
  combinedMode_ = false;
  return true;
}

auto WaterLevelSensor::fetchSensorBuffer(std::array<uint8_t, TOTAL_SECTIONS>& buffer) -> bool
{
  if (readSplit(buffer))
  {
    combinedMode_ = false;
    return true;
  }

  printf("[WaterLevelSensor] Split read failed, attempting combined fallback\n");

  const auto tryCombined = [&](const uint8_t address) -> bool
  {
    if (readCombined(address, buffer))
    {
      activeAddress_ = address;
      combinedMode_  = true;
      return true;
    }
    return false;
  };

  if (tryCombined(activeAddress_))
  {
    return true;
  }

  if ((lowAddress_ != highAddress_) && (activeAddress_ != lowAddress_) && tryCombined(lowAddress_))
  {
    return true;
  }

  if ((lowAddress_ != highAddress_) && (activeAddress_ != highAddress_) &&
      tryCombined(highAddress_))
  {
    return true;
  }

  return false;
}

void WaterLevelSensor::warmupIfStuck(std::array<uint8_t, TOTAL_SECTIONS>& buffer)
{
  if (hasSignal(std::span<const uint8_t>{buffer}))
  {
    startupPrimed_           = true;
    warmupAttemptsRemaining_ = 0;
    return;
  }

  if (startupPrimed_ or (warmupAttemptsRemaining_ == 0))
  {
    return;
  }

  if constexpr (Config::ENABLE_SERIAL_DEBUG)
  {
    printf("[WaterLevelSensor] INFO: Raw data stuck at 0, warmup attempts left=%u\n",
           warmupAttemptsRemaining_);
  }

  auto tryCombined = [&](const uint8_t address) -> bool
  {
    std::array<uint8_t, TOTAL_SECTIONS> temp{};
    if (readCombined(address, temp))
    {
      const bool hasData = hasSignal(std::span<const uint8_t>{temp});
      combinedMode_      = false;
      if (hasData)
      {
        buffer                   = temp;
        startupPrimed_           = true;
        warmupAttemptsRemaining_ = 0;
        return true;
      }
    }
    return false;
  };

  if (tryCombined(lowAddress_))
  {
    return;
  }

  if ((lowAddress_ != highAddress_) and tryCombined(highAddress_))
  {
    return;
  }

  if (Config::WATER_LEVEL_WAKE_DELAY_MS > 0U)
  {
    sleep_ms(Config::WATER_LEVEL_WAKE_DELAY_MS);
  }

  std::array<uint8_t, TOTAL_SECTIONS> refreshed{};
  if (readSplit(refreshed))
  {
    const bool hasData = hasSignal(std::span<const uint8_t>{refreshed});
    buffer             = refreshed;
    if (hasData)
    {
      startupPrimed_           = true;
      warmupAttemptsRemaining_ = 0;
      combinedMode_            = false;
      return;
    }
  }

  if (warmupAttemptsRemaining_ > 0U)
  {
    --warmupAttemptsRemaining_;
  }
}

auto WaterLevelSensor::buildSensorMap(const std::array<uint8_t, LOW_SECTIONS>&  low,
                                      const std::array<uint8_t, HIGH_SECTIONS>& high) -> uint32_t
{
  uint32_t sensorMap = 0;

  uint8_t idx = 0;
  for (const auto value : low)
  {
    if (value > Config::WATER_LEVEL_TOUCH_THRESHOLD)
    {
      sensorMap |= (1UL << idx);
    }
    ++idx;
  }

  idx = LOW_SECTIONS;
  for (const auto value : high)
  {
    if (value > Config::WATER_LEVEL_TOUCH_THRESHOLD)
    {
      sensorMap |= (1UL << idx);
    }
    ++idx;
  }

  return sensorMap;
}

auto WaterLevelSensor::countContinuousSections(const uint32_t sensorMap) -> uint8_t
{
  uint8_t contiguous = 0;

  for (uint8_t idx = 0; idx < TOTAL_SECTIONS; ++idx)
  {
    const bool isWet = ((sensorMap >> idx) & 0x01U) != 0U;
    if (isWet)
    {
      ++contiguous;
    }
    else
    {
      break;
    }
  }

  return contiguous;
}
