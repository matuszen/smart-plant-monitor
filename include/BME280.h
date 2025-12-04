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
  enum class SensorSampling : uint8_t
  {
    NONE = 0b000,
    X1   = 0b001,
    X2   = 0b010,
    X4   = 0b011,
    X8   = 0b100,
    X16  = 0b101
  };

  enum class SensorMode : uint8_t
  {
    SLEEP  = 0b00,
    FORCED = 0b01,
    NORMAL = 0b11
  };

  enum class SensorFilter : uint8_t
  {
    OFF = 0b000,
    X2  = 0b001,
    X4  = 0b010,
    X8  = 0b011,
    X16 = 0b100
  };

  enum class StandbyDuration : uint8_t
  {
    MS_0_5  = 0b000,
    MS_62_5 = 0b001,
    MS_125  = 0b010,
    MS_250  = 0b011,
    MS_500  = 0b100,
    MS_1000 = 0b101,
    MS_10   = 0b110,
    MS_20   = 0b111
  };

  struct ConfigRegister
  {
    uint8_t standby{static_cast<uint8_t>(StandbyDuration::MS_0_5)};
    uint8_t filter{static_cast<uint8_t>(SensorFilter::OFF)};
    uint8_t spi3w{0};

    [[nodiscard]] auto value() const -> uint8_t
    {
      return static_cast<uint8_t>(((standby & 0x07U) << 5U) | ((filter & 0x07U) << 2U) |
                                  (spi3w & 0x01U));
    }
  };

  struct CtrlMeasRegister
  {
    uint8_t osrsT{static_cast<uint8_t>(SensorSampling::X2)};
    uint8_t osrsP{static_cast<uint8_t>(SensorSampling::X16)};
    uint8_t mode{static_cast<uint8_t>(SensorMode::NORMAL)};

    [[nodiscard]] auto value() const -> uint8_t
    {
      return static_cast<uint8_t>(((osrsT & 0x07U) << 5U) | ((osrsP & 0x07U) << 2U) |
                                  (mode & 0x03U));
    }
  };

  struct CtrlHumRegister
  {
    uint8_t osrsH{static_cast<uint8_t>(SensorSampling::X1)};

    [[nodiscard]] auto value() const -> uint8_t
    {
      return static_cast<uint8_t>(osrsH & 0x07U);
    }
  };

  i2c_inst_t*      i2c_;
  uint8_t          address_;
  bool             initialized_{false};
  CalibrationData  calib_{};
  int32_t          t_fine_{0};
  int32_t          t_fine_adjust_{0};
  ConfigRegister   config_{};
  CtrlMeasRegister meas_{};
  CtrlHumRegister  hum_{};

  [[nodiscard]] auto readCalibrationData() -> bool;
  [[nodiscard]] auto readRawData() -> std::optional<std::array<uint8_t, 8>>;
  [[nodiscard]] auto setSampling(SensorMode mode, SensorSampling tempSampling,
                                 SensorSampling pressSampling, SensorSampling humSampling,
                                 SensorFilter filter, StandbyDuration duration) -> bool;

  [[nodiscard]] auto isReadingCalibration() -> bool;
  [[nodiscard]] auto compensateTemperature(int32_t adcT) -> float;
  [[nodiscard]] auto compensatePressure(int32_t adcP) const -> float;
  [[nodiscard]] auto compensateHumidity(int32_t adcH) const -> float;

  auto               writeRegister(uint8_t reg, uint8_t value) -> bool;
  [[nodiscard]] auto readRegister(uint8_t reg) -> std::optional<uint8_t>;
  [[nodiscard]] auto readRegisters(uint8_t reg, uint8_t* buffer, size_t length) -> bool;
};
