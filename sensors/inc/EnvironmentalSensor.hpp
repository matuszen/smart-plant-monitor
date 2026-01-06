#pragma once

#include "Types.hpp"

#include <hardware/i2c.h>

#include <cstddef>
#include <cstdint>
#include <optional>

class EnvironmentalSensor final
{
public:
  struct CalibrationData
  {
    uint16_t dig_T1 = 0;
    int16_t  dig_T2 = 0;
    int16_t  dig_T3 = 0;

    uint16_t dig_P1 = 0;
    int16_t  dig_P2 = 0;
    int16_t  dig_P3 = 0;
    int16_t  dig_P4 = 0;
    int16_t  dig_P5 = 0;
    int16_t  dig_P6 = 0;
    int16_t  dig_P7 = 0;
    int16_t  dig_P8 = 0;
    int16_t  dig_P9 = 0;

    uint8_t dig_H1 = 0;
    int16_t dig_H2 = 0;
    uint8_t dig_H3 = 0;
    int16_t dig_H4 = 0;
    int16_t dig_H5 = 0;
    int8_t  dig_H6 = 0;
  };

  explicit EnvironmentalSensor(i2c_inst_t* i2c, uint8_t address = 0x76);
  ~EnvironmentalSensor() = default;

  EnvironmentalSensor(const EnvironmentalSensor&)                    = delete;
  auto operator=(const EnvironmentalSensor&) -> EnvironmentalSensor& = delete;
  EnvironmentalSensor(EnvironmentalSensor&&)                         = delete;
  auto operator=(EnvironmentalSensor&&) -> EnvironmentalSensor&      = delete;

  auto init() -> bool;
  auto read() -> std::optional<EnvironmentData>;
  auto isAvailable() const -> bool
  {
    return initialized_;
  }

private:
  i2c_inst_t*     i2c_         = nullptr;
  uint8_t         address_     = 0;
  bool            initialized_ = false;
  CalibrationData calib_       = {};
  int32_t         tFine_       = 0;

  auto writeReg(uint8_t reg, uint8_t value) -> bool;
  auto readRegs(uint8_t reg, uint8_t* buf, size_t len) -> bool;
  auto readCalibrationData() -> bool;

  auto compensateTemp(int32_t adcT) -> int32_t;
  auto compensatePressure(int32_t adcP) const -> int32_t;
  auto compensateHumidity(int32_t adcH) const -> uint32_t;
};
