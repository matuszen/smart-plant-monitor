#include <array>
#include <cstdint>
#include <cstdio>

#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/structs/io_bank0.h>
#include <pico/stdio.h>
#include <pico/time.h>

namespace
{

inline constexpr uint8_t BME280_ADDR = 0x76;

inline constexpr i2c_inst_t* I2C_PORT    = i2c0;
inline constexpr uint32_t    I2C_BAUD    = 400'000;
inline constexpr uint8_t     I2C_SDA_PIN = 4;
inline constexpr uint8_t     I2C_SCL_PIN = 5;

inline constexpr uint8_t REG_ID        = 0xD0;
inline constexpr uint8_t REG_RESET     = 0xE0;
inline constexpr uint8_t REG_CTRL_HUM  = 0xF2;
inline constexpr uint8_t REG_STATUS    = 0xF3;
inline constexpr uint8_t REG_CTRL_MEAS = 0xF4;
inline constexpr uint8_t REG_CONFIG    = 0xF5;
inline constexpr uint8_t REG_PRESS_MSB = 0xF7;
inline constexpr uint8_t REG_CALIB_00  = 0x88;
inline constexpr uint8_t REG_CALIB_26  = 0xE1;

struct CalibData
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

int32_t   tFine{};
CalibData calib{};

void setupHardware()
{
  stdio_init_all();

  i2c_init(I2C_PORT, I2C_BAUD);
  gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);

  gpio_pull_up(I2C_SDA_PIN);
  gpio_pull_up(I2C_SCL_PIN);
}

void writeReg(uint8_t reg, uint8_t value)
{
  const std::array<uint8_t, 2> data{reg, value};
  i2c_write_blocking(I2C_PORT, BME280_ADDR, data.data(), 2, false);
}

void readRegs(uint8_t reg, uint8_t* buf, int len)
{
  i2c_write_blocking(I2C_PORT, BME280_ADDR, &reg, 1, true);
  i2c_read_blocking(I2C_PORT, BME280_ADDR, buf, len, false);
}

auto bme280Init() -> bool
{
  uint8_t id{};
  readRegs(REG_ID, &id, 1);
  if (id != 0x60)
  {
    printf("Error: BME280 not detected! Read ID: 0x%02X\n", id);
    return false;
  }
  printf("BME280 founded. ID: 0x%02X\n", id);

  writeReg(REG_RESET, 0xB6);
  sleep_ms(100);

  std::array<uint8_t, 26> buf{};
  readRegs(REG_CALIB_00, buf.data(), 24);
  calib.dig_T1 = (buf[1] << 8) | buf[0];
  calib.dig_T2 = (int16_t)((buf[3] << 8) | buf[2]);
  calib.dig_T3 = (int16_t)((buf[5] << 8) | buf[4]);
  calib.dig_P1 = (buf[7] << 8) | buf[6];
  calib.dig_P2 = (int16_t)((buf[9] << 8) | buf[8]);
  calib.dig_P3 = (int16_t)((buf[11] << 8) | buf[10]);
  calib.dig_P4 = (int16_t)((buf[13] << 8) | buf[12]);
  calib.dig_P5 = (int16_t)((buf[15] << 8) | buf[14]);
  calib.dig_P6 = (int16_t)((buf[17] << 8) | buf[16]);
  calib.dig_P7 = (int16_t)((buf[19] << 8) | buf[18]);
  calib.dig_P8 = (int16_t)((buf[21] << 8) | buf[20]);
  calib.dig_P9 = (int16_t)((buf[23] << 8) | buf[22]);

  readRegs(0xA1, &calib.dig_H1, 1);

  readRegs(REG_CALIB_26, buf.data(), 7);
  calib.dig_H2 = (int16_t)((buf[1] << 8) | buf[0]);
  calib.dig_H3 = buf[2];
  calib.dig_H4 = (int16_t)((buf[3] << 4) | (buf[4] & 0x0F));
  calib.dig_H5 = (int16_t)((buf[5] << 4) | (buf[4] >> 4));
  calib.dig_H6 = (int8_t)buf[6];

  writeReg(REG_CTRL_HUM, 0x01);

  writeReg(REG_CONFIG, (0x05 << 5) | (0x00 << 2));

  writeReg(REG_CTRL_MEAS, (0x01 << 5) | (0x01 << 2) | 0x03);

  return true;
}

auto compensateTemp(int32_t adcT) -> int32_t
{
  int32_t var1{};
  int32_t var2{};
  var1 = ((((adcT >> 3) - ((int32_t)calib.dig_T1 << 1))) * ((int32_t)calib.dig_T2)) >> 11;
  var2 = (((((adcT >> 4) - ((int32_t)calib.dig_T1)) * ((adcT >> 4) - ((int32_t)calib.dig_T1))) >> 12) *
          ((int32_t)calib.dig_T3)) >>
         14;
  tFine = var1 + var2;
  return (tFine * 5 + 128) >> 8;
}

auto compensatePressure(int32_t adcP) -> uint32_t
{
  int64_t var1{};
  int64_t var2{};
  int64_t p{};
  var1 = ((int64_t)tFine) - 128000;
  var2 = var1 * var1 * (int64_t)calib.dig_P6;
  var2 = var2 + ((var1 * (int64_t)calib.dig_P5) << 17);
  var2 = var2 + (((int64_t)calib.dig_P4) << 35);
  var1 = ((var1 * var1 * (int64_t)calib.dig_P3) >> 8) + ((var1 * (int64_t)calib.dig_P2) << 12);
  var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)calib.dig_P1) >> 33;

  if (var1 == 0)
  {
    return 0;
  }

  p    = 1048576 - adcP;
  p    = (((p << 31) - var2) * 3125) / var1;
  var1 = (((int64_t)calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  var2 = (((int64_t)calib.dig_P8) * p) >> 19;
  p    = ((p + var1 + var2) >> 8) + (((int64_t)calib.dig_P7) << 4);

  return static_cast<uint32_t>(p);
}

auto compensateHumidity(int32_t adcH) -> uint32_t
{
  int32_t vX1U32r{};
  vX1U32r = (tFine - ((int32_t)76800));
  vX1U32r =
    (((((adcH << 14) - (((int32_t)calib.dig_H4) << 20) - (((int32_t)calib.dig_H5) * vX1U32r)) + ((int32_t)16384)) >>
      15) *
     (((((((vX1U32r * ((int32_t)calib.dig_H6)) >> 10) *
          (((vX1U32r * ((int32_t)calib.dig_H3)) >> 11) + ((int32_t)32768))) >>
         10) +
        ((int32_t)2097152)) *
         ((int32_t)calib.dig_H2) +
       8192) >>
      14));
  vX1U32r = (vX1U32r - (((((vX1U32r >> 15) * (vX1U32r >> 15)) >> 7) * ((int32_t)calib.dig_H1)) >> 4));
  vX1U32r = (vX1U32r < 0 ? 0 : vX1U32r);
  vX1U32r = (vX1U32r > 419430400 ? 419430400 : vX1U32r);
  return static_cast<uint32_t>(vX1U32r >> 12);
}

}  // namespace

auto main() -> int
{
  setupHardware();
  sleep_ms(5'000);
  printf("Start environment sensor test...\n");

  if (not bme280Init())
  {
    while (true)
    {
      printf("Error during initialization. Check connections.\n");
      sleep_ms(1'000);
    }
  }

  while (true)
  {
    std::array<uint8_t, 8> data{};
    readRegs(REG_PRESS_MSB, data.data(), 8);

    const auto adcP = static_cast<int32_t>((data[0] << 12) | (data[1] << 4) | (data[2] >> 4));
    const auto adcT = static_cast<int32_t>((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));
    const auto adcH = static_cast<int32_t>((data[6] << 8) | data[7]);

    const auto temp  = compensateTemp(adcT);
    const auto press = compensatePressure(adcP);
    const auto hum   = compensateHumidity(adcH);

    const auto tempF  = temp / 100.0F;
    const auto pressF = press / 256.0F;
    const auto humF   = hum / 1024.0F;

    printf("Temperature: %.2f C | Pressure: %.2f hPa | Humidity: %.2f %%\n", tempF, pressF / 100.0F, humF);

    sleep_ms(1'000);
  }

  return 0;
}
