#include <array>
#include <cstdint>
#include <cstring>
#include <optional>

#include <hardware/i2c.h>
#include <pico/time.h>

#include "BME280.h"

namespace Reg
{

constexpr uint8_t ID        = 0xD0;
constexpr uint8_t RESET     = 0xE0;
constexpr uint8_t CTRL_HUM  = 0xF2;
constexpr uint8_t STATUS    = 0xF3;
constexpr uint8_t CTRL_MEAS = 0xF4;
constexpr uint8_t CONFIG    = 0xF5;
constexpr uint8_t PRESS_MSB = 0xF7;
constexpr uint8_t CALIB00   = 0x88;
constexpr uint8_t CALIB26   = 0xE1;
constexpr uint8_t CHIP_ID   = 0x60;

}  // namespace Reg

BME280::BME280(i2c_inst_t* i2c, const uint8_t address) : i2c_(i2c), address_(address)
{
}

auto BME280::init() -> bool
{
  const auto id = readRegister(Reg::ID);
  if (not id or id.value() != Reg::CHIP_ID)
  {
    return false;
  }

  writeRegister(Reg::RESET, 0xB6);
  sleep_ms(10);

  if (not readCalibrationData())
  {
    return false;
  }

  writeRegister(Reg::CTRL_HUM, 0x01);
  writeRegister(Reg::CTRL_MEAS, 0x27);
  writeRegister(Reg::CONFIG, 0x00);

  sleep_ms(100);

  initialized_ = true;
  return true;
}

auto BME280::read() -> std::optional<BME280::Measurement>
{
  if (not initialized_)
  {
    return std::nullopt;
  }

  const auto rawData = readRawData();
  if (not rawData)
  {
    return std::nullopt;
  }

  const auto& data = *rawData;

  const auto adcP = (static_cast<int32_t>(data[0]) << 12) | (static_cast<int32_t>(data[1]) << 4) |
                    (static_cast<int32_t>(data[2]) >> 4);
  const auto adcT = (static_cast<int32_t>(data[3]) << 12) | (static_cast<int32_t>(data[4]) << 4) |
                    (static_cast<int32_t>(data[5]) >> 4);
  const auto adcH = (static_cast<int32_t>(data[6]) << 8) | static_cast<int32_t>(data[7]);

  const auto temp  = compensateTemperature(adcT);
  const auto press = compensatePressure(adcP);
  const auto hum   = compensateHumidity(adcH);

  auto measurement        = Measurement{};
  measurement.temperature = float(temp) / 100.0F;
  measurement.pressure    = float(press) / 25'600.0F;
  measurement.humidity    = float(hum) / 1'024.0F;

  return measurement;
}

auto BME280::readCalibrationData() -> bool
{
  auto calib00 = std::array<uint8_t, 26>{};
  auto calib26 = std::array<uint8_t, 7>{};

  if (not readRegisters(Reg::CALIB00, calib00.data(), calib00.size()))
  {
    return false;
  }

  if (not readRegisters(Reg::CALIB26, calib26.data(), calib26.size()))
  {
    return false;
  }

  calib_.dig_T1 = (calib00[1] << 8) | calib00[0];
  calib_.dig_T2 = (calib00[3] << 8) | calib00[2];
  calib_.dig_T3 = (calib00[5] << 8) | calib00[4];

  calib_.dig_P1 = (calib00[7] << 8) | calib00[6];
  calib_.dig_P2 = (calib00[9] << 8) | calib00[8];
  calib_.dig_P3 = (calib00[11] << 8) | calib00[10];
  calib_.dig_P4 = (calib00[13] << 8) | calib00[12];
  calib_.dig_P5 = (calib00[15] << 8) | calib00[14];
  calib_.dig_P6 = (calib00[17] << 8) | calib00[16];
  calib_.dig_P7 = (calib00[19] << 8) | calib00[18];
  calib_.dig_P8 = (calib00[21] << 8) | calib00[20];
  calib_.dig_P9 = (calib00[23] << 8) | calib00[22];

  calib_.dig_H1 = calib00[25];
  calib_.dig_H2 = (calib26[1] << 8) | calib26[0];
  calib_.dig_H3 = calib26[2];
  calib_.dig_H4 = (calib26[3] << 4) | (calib26[4] & 0x0F);
  calib_.dig_H5 = (calib26[5] << 4) | (calib26[4] >> 4);
  calib_.dig_H6 = (calib26[6]);

  return true;
}

auto BME280::readRawData() -> std::optional<std::array<uint8_t, 8>>
{
  auto data = std::array<uint8_t, 8>{};
  if (not readRegisters(Reg::PRESS_MSB, data.data(), 8))
  {
    return std::nullopt;
  }
  return data;
}

auto BME280::compensateTemperature(const int32_t adcT) -> int32_t
{
  const auto var1 = ((((adcT >> 3) - (static_cast<int32_t>(calib_.dig_T1) << 1))) *
                     static_cast<int32_t>(calib_.dig_T2)) >>
                    11;
  const auto var2 = (((((adcT >> 4) - static_cast<int32_t>(calib_.dig_T1)) *
                       ((adcT >> 4) - static_cast<int32_t>(calib_.dig_T1))) >>
                      12) *
                     static_cast<int32_t>(calib_.dig_T3)) >>
                    14;
  t_fine_ = var1 + var2;
  return (t_fine_ * 5 + 128) >> 8;
}

auto BME280::compensatePressure(const int32_t adcP) const -> uint32_t
{
  auto var1 = static_cast<int64_t>(t_fine_) - 128'000;
  auto var2 = var1 * var1 * static_cast<int64_t>(calib_.dig_P6);
  var2      = var2 + ((var1 * static_cast<int64_t>(calib_.dig_P5)) << 17);
  var2      = var2 + ((static_cast<int64_t>(calib_.dig_P4)) << 35);
  var1      = ((var1 * var1 * static_cast<int64_t>(calib_.dig_P3)) >> 8) +
         ((var1 * static_cast<int64_t>(calib_.dig_P2)) << 12);
  var1 = ((((static_cast<int64_t>(1)) << 47) + var1)) * static_cast<int64_t>(calib_.dig_P1) >> 33;

  if (var1 == 0)
  {
    return 0;
  }

  int64_t p = 1'048'576 - adcP;
  p         = (((p << 31) - var2) * 3'125) / var1;
  var1      = (static_cast<int64_t>(calib_.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  var2      = (static_cast<int64_t>(calib_.dig_P8) * p) >> 19;
  p         = ((p + var1 + var2) >> 8) + ((static_cast<int64_t>(calib_.dig_P7)) << 4);

  return static_cast<uint32_t>(p);
}

auto BME280::compensateHumidity(const int32_t adcH) const -> uint32_t
{
  auto vX1U32r = t_fine_ - 76'800;
  vX1U32r      = (((((adcH << 14) - (static_cast<int32_t>(calib_.dig_H4) << 20) -
                (static_cast<int32_t>(calib_.dig_H5) * vX1U32r)) +
               16'384) >>
              15) *
             (((((((vX1U32r * static_cast<int32_t>(calib_.dig_H6)) >> 10) *
                  (((vX1U32r * static_cast<int32_t>(calib_.dig_H3)) >> 11) + 32'768)) >>
                 10) +
                2'097'152) *
                 static_cast<int32_t>(calib_.dig_H2) +
               8'192) >>
              14));
  vX1U32r =
    vX1U32r -
    (((((vX1U32r >> 15) * (vX1U32r >> 15)) >> 7) * static_cast<int32_t>(calib_.dig_H1)) >> 4);
  vX1U32r = (vX1U32r < 0) ? 0 : vX1U32r;
  vX1U32r = (vX1U32r > 419'430'400) ? 419'430'400 : vX1U32r;

  return static_cast<uint32_t>(vX1U32r >> 12);
}

auto BME280::writeRegister(const uint8_t reg, const uint8_t value) -> bool
{
  auto data = std::array<uint8_t, 2>{reg, value};
  return i2c_write_blocking(i2c_, address_, data.data(), data.size(), false) ==
         static_cast<int>(data.size());
}

auto BME280::readRegister(const uint8_t reg) -> std::optional<uint8_t>
{
  auto value = uint8_t{};
  if (i2c_write_blocking(i2c_, address_, &reg, 1, true) != 1)
  {
    return std::nullopt;
  }
  if (i2c_read_blocking(i2c_, address_, &value, 1, false) != 1)
  {
    return std::nullopt;
  }
  return value;
}

auto BME280::readRegisters(const uint8_t reg, uint8_t* buffer, const size_t length) -> bool
{
  if (i2c_write_blocking(i2c_, address_, &reg, 1, true) != 1)
  {
    return false;
  }
  return i2c_read_blocking(i2c_, address_, buffer, length, false) == static_cast<int>(length);
}
