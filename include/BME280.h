#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "hardware/i2c.h"

class BME280
{
public:
  struct CalibrationData
  {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;

    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;

    uint8_t dig_H1;
    int16_t dig_H2;
    uint8_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t  dig_H6;
  };

  struct Measurement
  {
    float temperature{0.0F};
    float pressure{0.0F};
    float humidity{0.0F};
  };

  explicit BME280(i2c_inst_t* i2c, uint8_t address = 0x76);
  ~BME280() = default;

  BME280(const BME280&)                    = delete;
  auto operator=(const BME280&) -> BME280& = delete;
  BME280(BME280&&)                         = delete;
  auto operator=(BME280&&) -> BME280&      = delete;

  [[nodiscard]] auto init() -> bool;
  [[nodiscard]] auto read() -> std::optional<Measurement>;
  [[nodiscard]] auto isAvailable() const noexcept -> bool
  {
    return initialized_;
  }

private:
  i2c_inst_t*     i2c_;
  uint8_t         address_;
  bool            initialized_{false};
  CalibrationData calib_{};
  int32_t         t_fine_{0};

  [[nodiscard]] auto readCalibrationData() -> bool;
  [[nodiscard]] auto readRawData() -> std::optional<std::array<uint8_t, 8>>;
  [[nodiscard]] auto compensateTemperature(int32_t adcT) -> int32_t;
  [[nodiscard]] auto compensatePressure(int32_t adcP) const -> uint32_t;
  [[nodiscard]] auto compensateHumidity(int32_t adcH) const -> uint32_t;

  auto               writeRegister(uint8_t reg, uint8_t value) -> bool;
  [[nodiscard]] auto readRegister(uint8_t reg) -> std::optional<uint8_t>;
  [[nodiscard]] auto readRegisters(uint8_t reg, uint8_t* buffer, size_t length) -> bool;
};
