#include "LedTask.hpp"
#include "AppContext.hpp"

#include "Common.hpp"
#include "Config.hpp"

#include <FreeRTOS.h>
#include <hardware/gpio.h>
#include <pico/stdlib.h>
#include <projdefs.h>
#include <task.h>

#include <cstdint>

void ledTask(void* const params)
{
  auto* ctx = static_cast<AppContext*>(params);

  bool     networkOn         = false;
  uint32_t lastNetworkToggle = 0;

  constexpr uint32_t connectBlinkMs   = 400;
  constexpr uint32_t provisionBlinkMs = 150;
  constexpr uint32_t connectedBlinkMs = 1000;

  while (true)
  {
    const auto now = Utils::getTimeSinceBoot();
    const auto led = ctx->readLedState();

    gpio_put(Config::LED_STATUS_PIN, led.activity);

    switch (led.network)
    {
      case NetworkLedState::MQTT_CONNECTED:
        networkOn = true;
        break;
      case NetworkLedState::CONNECTED:
        if ((now - lastNetworkToggle) >= connectedBlinkMs)
        {
          networkOn         = not networkOn;
          lastNetworkToggle = now;
        }
        break;
      case NetworkLedState::PROVISIONING:
        if ((now - lastNetworkToggle) >= provisionBlinkMs)
        {
          networkOn         = not networkOn;
          lastNetworkToggle = now;
        }
        break;
      case NetworkLedState::CONNECTING:
        if ((now - lastNetworkToggle) >= connectBlinkMs)
        {
          networkOn         = not networkOn;
          lastNetworkToggle = now;
        }
        break;
      case NetworkLedState::OFF:
      default:
        networkOn = false;
        break;
    }
    gpio_put(Config::LED_NETWORK_PIN, networkOn);
    gpio_put(Config::LED_ERROR_PIN, led.isError());

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
