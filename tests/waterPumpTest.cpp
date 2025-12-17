#include <cstdint>
#include <cstdio>

#include <hardware/gpio.h>
#include <pico/stdio.h>
#include <pico/time.h>

namespace
{

inline constexpr uint8_t TRANSISTOR_BASE_PIN = 2;

void setupHardware()
{
  stdio_init_all();

  gpio_init(TRANSISTOR_BASE_PIN);
  gpio_set_dir(TRANSISTOR_BASE_PIN, GPIO_OUT);
}

}  // namespace

auto main() -> int
{
  setupHardware();
  sleep_ms(5'000);
  printf("Starting water pump test...\n");

  while (true)
  {
    printf("Turning water pump ON...\n");
    gpio_put(TRANSISTOR_BASE_PIN, true);
    sleep_ms(3'000);
    printf("Turning water pump OFF...\n");
    gpio_put(TRANSISTOR_BASE_PIN, false);
    sleep_ms(3'000);
  }

  return 0;
}
