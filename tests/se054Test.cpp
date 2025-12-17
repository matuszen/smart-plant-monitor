#include <cstdint>
#include <cstdio>

#include <hardware/gpio.h>
#include <pico/stdio.h>
#include <pico/time.h>

namespace
{

inline constexpr uint8_t HALL_POWER_PIN  = 0;
inline constexpr uint8_t HALL_SIGNAL_PIN = 1;

void setupHardware()
{
  stdio_init_all();

  gpio_init(HALL_POWER_PIN);
  gpio_set_dir(HALL_POWER_PIN, GPIO_OUT);
  gpio_put(HALL_POWER_PIN, true);

  gpio_init(HALL_SIGNAL_PIN);
  gpio_set_dir(HALL_SIGNAL_PIN, GPIO_IN);
  gpio_pull_up(HALL_SIGNAL_PIN);
}

}  // namespace

auto main() -> int
{
  setupHardware();
  sleep_ms(5'000);
  printf("Start hall sensor test...\n");

  while (true)
  {
    const auto isMagnetDetected = not gpio_get(HALL_SIGNAL_PIN);

    if (isMagnetDetected)
    {
      printf("Magnet detected (Signal LOW)\n");
    }
    else
    {
      printf("No magnetic field (Signal HIGH)\n");
    }

    sleep_ms(1'000);
  }

  return 0;
}
