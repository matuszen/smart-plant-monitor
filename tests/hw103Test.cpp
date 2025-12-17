#include <algorithm>
#include <cstdint>
#include <cstdio>

#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <pico/stdio.h>
#include <pico/time.h>
#include <pico/types.h>

namespace
{

inline constexpr uint8_t SENSOR_POWER_PIN = 22;
inline constexpr uint8_t SENSOR_ADC_PIN   = 26;
inline constexpr uint8_t ADC_CHANNEL      = 0;

inline constexpr float DRY_VAL = 3500.0F;
inline constexpr float WET_VAL = 1500.0F;

void setupHardware()
{
  stdio_init_all();

  gpio_init(SENSOR_POWER_PIN);
  gpio_set_dir(SENSOR_POWER_PIN, GPIO_OUT);

  adc_init();
  adc_gpio_init(SENSOR_ADC_PIN);
  adc_select_input(ADC_CHANNEL);
}

}  // namespace

auto main() -> int
{
  setupHardware();
  sleep_ms(5'000);
  printf("Start soil moisture sensor test...\n");

  while (true)
  {
    gpio_put(SENSOR_POWER_PIN, true);
    sleep_ms(200);
    const auto rawValue = adc_read();
    gpio_put(SENSOR_POWER_PIN, false);

    const auto voltage = rawValue * 3.3F / (1 << 12);

    auto percentage = 100.0F * (1.0F - (rawValue - WET_VAL) / (DRY_VAL - WET_VAL));
    percentage      = std::max<float>(percentage, 0);
    percentage      = std::min<float>(percentage, 100);

    printf("ADC Raw: %4d | Voltage: %.2f V | Humidity: %.1f%%\n", rawValue, voltage, percentage);

    sleep_ms(1'000);
  }

  return 0;
}
