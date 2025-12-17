#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/structs/io_bank0.h>
#include <pico/error.h>
#include <pico/stdio.h>
#include <pico/time.h>

namespace
{

inline constexpr i2c_inst_t* I2C_PORT = i2c1;
inline constexpr uint32_t    I2C_BAUD = 100'000;
inline constexpr uint8_t     PIN_SDA  = 18;
inline constexpr uint8_t     PIN_SCL  = 19;

inline constexpr uint8_t ADDR_LOW  = 0x77;
inline constexpr uint8_t ADDR_HIGH = 0x78;

inline constexpr uint8_t PIN_SENSOR_POWER = 14;

inline constexpr uint8_t THRESHOLD = 100;

void setupHardware()
{
  stdio_init_all();

  gpio_init(PIN_SENSOR_POWER);
  gpio_set_dir(PIN_SENSOR_POWER, GPIO_OUT);
  gpio_put(PIN_SENSOR_POWER, true);

  i2c_init(I2C_PORT, I2C_BAUD);
  gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
  gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);

  gpio_pull_up(PIN_SDA);
  gpio_pull_up(PIN_SCL);
}

}  // namespace

auto main() -> int
{
  setupHardware();
  sleep_ms(3'000);
  printf("Starting water level sensor test..\n");

  std::array<uint8_t, 20> sensorData{};

  while (true)
  {
    std::ranges::fill(sensorData, 0);
    sleep_ms(2'000);

    const auto retLow  = i2c_read_blocking(I2C_PORT, ADDR_LOW, sensorData.data(), 8, false);
    const auto retHigh = i2c_read_blocking(I2C_PORT, ADDR_HIGH, std::span(sensorData).subspan(8, 12).data(), 12, false);

    if (retLow == PICO_ERROR_GENERIC and retHigh == PICO_ERROR_GENERIC)
    {
      printf("I2C error: Sensor did not respond (check power/delay).\n");
      continue;
    }

    const auto activeSections = std::ranges::count_if(sensorData, [&](uint8_t i) -> bool { return i > THRESHOLD; });
    const auto levelInCm      = activeSections * 0.5F;

    printf("Level: %5.1f cm |", levelInCm);
    for (int i = 0; i < 20; ++i)
    {
      printf(i < activeSections ? "#" : ".");
    }
    printf("|\n");
  }
  return 0;
}
