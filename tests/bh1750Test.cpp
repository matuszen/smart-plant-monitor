#include <array>
#include <cstdint>
#include <cstdio>

#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/structs/io_bank0.h>
#include <pico/stdio.h>
#include <pico/time.h>

inline constexpr uint8_t BH1750_ADDR = 0x23;

inline constexpr uint8_t BH1750_POWER_ON                 = 0x01;
inline constexpr uint8_t BH1750_RESET                    = 0x07;
inline constexpr uint8_t BH1750_CONTINUOUS_HIGH_RES_MODE = 0x10;

inline constexpr i2c_inst_t* I2C_PORT    = i2c0;
inline constexpr uint8_t     I2C_SDA_PIN = 4;
inline constexpr uint8_t     I2C_SCL_PIN = 5;

inline constexpr float CORRECTION_FACTOR = 1.0F;

namespace
{

void bh1750Init()
{
  std::array<uint8_t, 1> buffer{};

  buffer[0] = BH1750_POWER_ON;
  i2c_write_blocking(I2C_PORT, BH1750_ADDR, buffer.data(), 1, false);

  buffer[0] = BH1750_CONTINUOUS_HIGH_RES_MODE;
  i2c_write_blocking(I2C_PORT, BH1750_ADDR, buffer.data(), 1, false);

  sleep_ms(200);
}

auto bh1750ReadLux() -> float
{
  std::array<uint8_t, 2> buffer{};

  const auto count = i2c_read_blocking(I2C_PORT, BH1750_ADDR, buffer.data(), 2, false);
  if (count != 2)
  {
    printf("Error reading I2C!\n");
    return -1.0F;
  }

  const auto val = (static_cast<uint16_t>(buffer[0]) << 8) | buffer[1];
  return (static_cast<float>(val) / 1.2F) * CORRECTION_FACTOR;
}

}  // namespace

auto main() -> int
{
  stdio_init_all();
  sleep_ms(5'000);

  i2c_init(I2C_PORT, 400'000);

  gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);

  gpio_pull_up(I2C_SDA_PIN);
  gpio_pull_up(I2C_SCL_PIN);

  printf("Starting BH1750 on Pico 2...\n");

  bh1750Init();

  while (true)
  {
    const auto lux = bh1750ReadLux();
    if (lux >= 0.0F)
    {
      printf("Light intensity: %.2f lux\n", lux);
    }
    sleep_ms(1'000);
  }

  return 0;
}
