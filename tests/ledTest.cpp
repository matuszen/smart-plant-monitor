#include <cstdint>
#include <cstdio>

#include <hardware/gpio.h>
#include <pico/stdio.h>
#include <pico/time.h>

namespace
{

inline constexpr uint8_t BUTTON_PIN      = 0;
inline constexpr uint8_t LED_STATUS_PIN  = 1;
inline constexpr uint8_t LED_NETWORK_PIN = 3;
inline constexpr uint8_t LED_ERROR_PIN   = 7;

void setupHardware()
{
  stdio_init_all();

  gpio_init(LED_STATUS_PIN);
  gpio_init(LED_NETWORK_PIN);
  gpio_init(LED_ERROR_PIN);

  gpio_set_dir(LED_STATUS_PIN, GPIO_OUT);
  gpio_set_dir(LED_NETWORK_PIN, GPIO_OUT);
  gpio_set_dir(LED_ERROR_PIN, GPIO_OUT);

  gpio_init(BUTTON_PIN);
  gpio_set_dir(BUTTON_PIN, GPIO_IN);

  gpio_pull_down(BUTTON_PIN);
}

}  // namespace

auto main() -> int
{
  setupHardware();

  printf("Starting up test...\n");
  sleep_ms(5'000);

  while (true)
  {
    const auto buttonState = gpio_get(BUTTON_PIN);

    if (buttonState)
    {
      printf("Button pressed\n");
      gpio_put(LED_STATUS_PIN, true);
      sleep_ms(300);
      gpio_put(LED_STATUS_PIN, false);
      sleep_ms(300);
      gpio_put(LED_NETWORK_PIN, true);
      sleep_ms(300);
      gpio_put(LED_NETWORK_PIN, false);
      sleep_ms(300);
      gpio_put(LED_ERROR_PIN, true);
      sleep_ms(300);
      gpio_put(LED_ERROR_PIN, false);
    }

    sleep_ms(10);
  }
}
