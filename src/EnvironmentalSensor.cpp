#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>

#include <hardware/i2c.h>
#include <pico/time.h>

#include "EnvironmentalSensor.h"
#include "Types.h"

namespace
{

inline constexpr uint8_t REG_ID        = 0xD0;
inline constexpr uint8_t REG_RESET     = 0xE0;
inline constexpr uint8_t REG_CTRL_HUM  = 0xF2;
inline constexpr uint8_t REG_STATUS    = 0xF3;
inline constexpr uint8_t REG_CTRL_MEAS = 0xF4;
inline constexpr uint8_t REG_CONFIG    = 0xF5;
inline constexpr uint8_t REG_PRESS_MSB = 0xF7;
inline constexpr uint8_t REG_CALIB_00  = 0x88;
inline constexpr uint8_t REG_CALIB_26  = 0xE1;

inline constexpr uint8_t CHIP_ID = 0x60;

}  // namespace

EnvironmentalSensor::EnvironmentalSensor(i2c_inst_t* i2c, const uint8_t address) : i2c_(i2c), address_(address)
{
}

auto EnvironmentalSensor::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  if (i2c_ == nullptr)
  {
    printf("[BME280] ERROR: Missing I2C instance\n");
    return false;
  }

  uint8_t id{};
  if (not readRegs(REG_ID, &id, 1) or id != CHIP_ID)
  {
    printf("[BME280] Not detected (ID=0x%02X)\n", id);
    return false;
  }

  writeReg(REG_RESET, 0xB6);
  sleep_ms(100);

  if (not readCalibrationData())
  {
    printf("[BME280] Failed to read calibration data\n");
    return false;
  }

  writeReg(REG_CTRL_HUM, 0x01);
  writeReg(REG_CONFIG, (0x05U << 5U) | (0x00U << 2U));
  writeReg(REG_CTRL_MEAS, (0x01U << 5U) | (0x01U << 2U) | 0x03U);

  initialized_ = true;
  printf("[BME280] Initialized (addr=0x%02X)\n", address_);
  return true;
}

auto EnvironmentalSensor::read() -> std::optional<EnvironmentData>
{
  if (not initialized_ and not init())
  {
    return std::nullopt;
  }

  std::array<uint8_t, 8> data{};
  if (not readRegs(REG_PRESS_MSB, data.data(), data.size()))
  {
    printf("[BME280] Read failed\n");
    initialized_ = false;
    return std::nullopt;
  }

  const auto adcP = static_cast<int32_t>((data[0] << 12) | (data[1] << 4) | (data[2] >> 4));
  const auto adcT = static_cast<int32_t>((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));
  const auto adcH = static_cast<int32_t>((data[6] << 8) | data[7]);

  const auto temp  = compensateTemp(adcT);
  const auto press = compensatePressure(adcP);
  const auto hum   = compensateHumidity(adcH);

  if (press <= 0)
  {
    printf("[BME280] Pressure compensation failed (raw=%d)\n", press);
    return std::nullopt;
  }

  const auto measurement = EnvironmentData{
    .temperature = temp / 100.0F,
    .humidity    = static_cast<float>(hum) / 1'024.0F,
    .pressure    = static_cast<float>(press) / 25'600.0F,
    .valid       = true,
  };
  return measurement;
}

auto EnvironmentalSensor::writeReg(const uint8_t reg, const uint8_t value) -> bool
{
  const std::array<uint8_t, 2> data{reg, value};
  return i2c_write_blocking(i2c_, address_, data.data(), data.size(), false) == static_cast<int>(data.size());
}

auto EnvironmentalSensor::readRegs(const uint8_t reg, uint8_t* const buf, const size_t len) -> bool
{
  if (i2c_write_blocking(i2c_, address_, &reg, 1, true) != 1)
  {
    return false;
  }
  return i2c_read_blocking(i2c_, address_, buf, len, false) == static_cast<int>(len);
}

auto EnvironmentalSensor::readCalibrationData() -> bool
{
  std::array<uint8_t, 26> buf{};
  if (not readRegs(REG_CALIB_00, buf.data(), 24))
  {
    return false;
  }

  calib_.dig_T1 = (buf[1] << 8) | buf[0];
  calib_.dig_T2 = static_cast<int16_t>((buf[3] << 8) | buf[2]);
  calib_.dig_T3 = static_cast<int16_t>((buf[5] << 8) | buf[4]);
  calib_.dig_P1 = (buf[7] << 8) | buf[6];
  calib_.dig_P2 = static_cast<int16_t>((buf[9] << 8) | buf[8]);
  calib_.dig_P3 = static_cast<int16_t>((buf[11] << 8) | buf[10]);
  calib_.dig_P4 = static_cast<int16_t>((buf[13] << 8) | buf[12]);
  calib_.dig_P5 = static_cast<int16_t>((buf[15] << 8) | buf[14]);
  calib_.dig_P6 = static_cast<int16_t>((buf[17] << 8) | buf[16]);
  calib_.dig_P7 = static_cast<int16_t>((buf[19] << 8) | buf[18]);
  calib_.dig_P8 = static_cast<int16_t>((buf[21] << 8) | buf[20]);
  calib_.dig_P9 = static_cast<int16_t>((buf[23] << 8) | buf[22]);

  if (not readRegs(REG_CALIB_26, buf.data(), 7))
  {
    return false;
  }

  calib_.dig_H1 = 0;
  calib_.dig_H2 = static_cast<int16_t>((buf[1] << 8) | buf[0]);
  calib_.dig_H3 = buf[2];
  calib_.dig_H4 = static_cast<int16_t>((buf[3] << 4) | (buf[4] & 0x0F));
  calib_.dig_H5 = static_cast<int16_t>((buf[5] << 4) | (buf[4] >> 4));
  calib_.dig_H6 = static_cast<int8_t>(buf[6]);

  uint8_t h1{};
  if (readRegs(0xA1, &h1, 1))
  {
    calib_.dig_H1 = h1;
  }

  return true;
}

auto EnvironmentalSensor::compensateTemp(const int32_t adcT) -> int32_t
{
  const auto var1 =
    ((((adcT >> 3) - (static_cast<int32_t>(calib_.dig_T1) << 1))) * static_cast<int32_t>(calib_.dig_T2)) >> 11;
  const auto var2 =
    (((((adcT >> 4) - static_cast<int32_t>(calib_.dig_T1)) * ((adcT >> 4) - static_cast<int32_t>(calib_.dig_T1))) >>
      12) *
     static_cast<int32_t>(calib_.dig_T3)) >>
    14;
  tFine_ = var1 + var2;
  return (tFine_ * 5 + 128) >> 8;
}

auto EnvironmentalSensor::compensatePressure(const int32_t adcP) const -> int32_t
{
  auto var1 = static_cast<int64_t>(tFine_) - 128'000;
  auto var2 = var1 * var1 * static_cast<int64_t>(calib_.dig_P6);

  var2 = var2 + ((var1 * static_cast<int64_t>(calib_.dig_P5)) << 17);
  var2 = var2 + (static_cast<int64_t>(calib_.dig_P4) << 35);
  var1 =
    ((var1 * var1 * static_cast<int64_t>(calib_.dig_P3)) >> 8) + ((var1 * static_cast<int64_t>(calib_.dig_P2)) << 12);
  var1 = (((static_cast<int64_t>(1) << 47) + var1) * static_cast<int64_t>(calib_.dig_P1)) >> 33;

  if (var1 == 0)
  {
    return 0;
  }

  auto p = 1'048'576 - adcP;
  p      = (((p << 31) - var2) * 3'125) / var1;
  var1   = (static_cast<int64_t>(calib_.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  var2   = (static_cast<int64_t>(calib_.dig_P8) * p) >> 19;
  p      = ((p + var1 + var2) >> 8) + (static_cast<int64_t>(calib_.dig_P7) << 4);

  return static_cast<int32_t>(p);
}

auto EnvironmentalSensor::compensateHumidity(const int32_t adcH) const -> uint32_t
{
  auto vX1U32r = tFine_ - 76'800;
  vX1U32r =
    (((((adcH << 14) - (static_cast<int32_t>(calib_.dig_H4) << 20) - (static_cast<int32_t>(calib_.dig_H5) * vX1U32r)) +
       16'384) >>
      15) *
     (((((((vX1U32r * static_cast<int32_t>(calib_.dig_H6)) >> 10) *
          (((vX1U32r * static_cast<int32_t>(calib_.dig_H3)) >> 11) + 32'768)) >>
         10) +
        2'097'152) *
         static_cast<int32_t>(calib_.dig_H2) +
       8'192) >>
      14));
  vX1U32r = vX1U32r - (((((vX1U32r >> 15) * (vX1U32r >> 15)) >> 7) * static_cast<int32_t>(calib_.dig_H1)) >> 4);
  vX1U32r = (vX1U32r < 0) ? 0 : vX1U32r;
  vX1U32r = (vX1U32r > 419'430'400) ? 419'430'400 : vX1U32r;
  return static_cast<uint32_t>(vX1U32r >> 12);
}
