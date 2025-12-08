#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <hardware/i2c.h>

#include "Config.h"

class WaterLevelSensor final
{
public:
  struct Reading
  {
    uint8_t  sections{0};
    uint16_t depthMm{0};
    float    percentage{0.0F};
  };

  explicit WaterLevelSensor(i2c_inst_t* i2c, uint8_t lowAddress = Config::WATER_LEVEL_LOW_ADDR,
                            uint8_t highAddress = Config::WATER_LEVEL_HIGH_ADDR);
  ~WaterLevelSensor() = default;

  WaterLevelSensor(const WaterLevelSensor&)                    = delete;
  auto operator=(const WaterLevelSensor&) -> WaterLevelSensor& = delete;
  WaterLevelSensor(WaterLevelSensor&&)                         = delete;
  auto operator=(WaterLevelSensor&&) -> WaterLevelSensor&      = delete;

  [[nodiscard]] auto init() -> bool;
  [[nodiscard]] auto read() -> std::optional<Reading>;
  [[nodiscard]] auto isAvailable() const noexcept -> bool
  {
    return initialized_;
  }

private:
  static constexpr uint8_t LOW_SECTIONS{8};
  static constexpr uint8_t HIGH_SECTIONS{12};
  static constexpr uint8_t TOTAL_SECTIONS{LOW_SECTIONS + HIGH_SECTIONS};
  static_assert(TOTAL_SECTIONS == Config::WATER_LEVEL_TOTAL_SECTIONS,
                "Water level section count mismatch");
  static_assert(TOTAL_SECTIONS <= 32, "Water level algorithm assumes up to 32 contiguous segments");

  i2c_inst_t* i2c_;
  uint8_t     lowAddress_;
  uint8_t     highAddress_;
  bool        initialized_{false};
  uint8_t     activeAddress_{0};
  bool        combinedMode_{false};
  bool        startupPrimed_{false};
  uint8_t     warmupAttemptsRemaining_{Config::WATER_LEVEL_WAKE_RETRIES};

  [[nodiscard]] auto readBlock(uint8_t address, uint8_t* buffer, size_t length) -> bool;
  [[nodiscard]] auto readCombined(uint8_t                              address,
                                  std::array<uint8_t, TOTAL_SECTIONS>& buffer) -> bool;
  [[nodiscard]] auto readSplit(std::array<uint8_t, TOTAL_SECTIONS>& buffer) -> bool;
  [[nodiscard]] auto fetchSensorBuffer(std::array<uint8_t, TOTAL_SECTIONS>& buffer) -> bool;
  void               warmupIfStuck(std::array<uint8_t, TOTAL_SECTIONS>& buffer);
  [[nodiscard]] static auto
                            buildSensorMap(const std::array<uint8_t, LOW_SECTIONS>&  low,
                                           const std::array<uint8_t, HIGH_SECTIONS>& high) -> uint32_t;
  [[nodiscard]] static auto countContinuousSections(uint32_t sensorMap) -> uint8_t;
};
