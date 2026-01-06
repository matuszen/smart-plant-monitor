#include "ButtonTask.hpp"
#include "AppContext.hpp"

#include "Common.hpp"
#include "Config.hpp"

#include <FreeRTOS.h>
#include <hardware/gpio.h>
#include <pico/stdlib.h>
#include <projdefs.h>
#include <task.h>

#include <cstdint>
#include <cstdio>

namespace
{

void handleButtonRelease(AppContext& ctx, const uint32_t heldMs, bool& rebootSent)
{
  if (rebootSent)
  {
    rebootSent = false;
    return;
  }

  printf("[Button] Released after %u ms\n", heldMs);

  if (heldMs >= Config::BUTTON_AP_MIN_MS and heldMs < Config::BUTTON_REBOOT_MS)
  {
    const WifiCommand cmd = WifiCommand::START_PROVISIONING;
    if (ctx.wifiCommandQueue != nullptr)
    {
      xQueueSend(ctx.wifiCommandQueue, &cmd, 0);
      if (ctx.apActive)
      {
        ctx.apCancel = true;
      }
      printf("[Button] AP toggle requested after %u ms hold\n", heldMs);
    }
  }
}

void handleButtonHold(AppContext& ctx, const uint32_t heldMs, bool& rebootSent)
{
  if (heldMs >= Config::BUTTON_REBOOT_MS)
  {
    const WifiCommand cmd = WifiCommand::REBOOT;
    if (ctx.wifiCommandQueue != nullptr)
    {
      xQueueSend(ctx.wifiCommandQueue, &cmd, 0);
      rebootSent   = true;
      ctx.apCancel = true;
      printf("[Button] Reboot requested after %u ms hold\n", heldMs);
    }
  }
}

}  // namespace

void buttonTask(void* const params)
{
  auto* ctx = static_cast<AppContext*>(params);

  bool     pressed    = false;
  bool     rebootSent = false;
  uint32_t pressedAt  = 0;

  while (true)
  {
    const bool isPressed = gpio_get(Config::BUTTON_PIN);

    if (isPressed and not pressed)
    {
      pressed   = true;
      pressedAt = Utils::getTimeSinceBoot();
    }
    else if (not isPressed and pressed)
    {
      handleButtonRelease(*ctx, Utils::getTimeSinceBoot() - pressedAt, rebootSent);
      pressed = false;
    }

    if (pressed and not rebootSent)
    {
      handleButtonHold(*ctx, Utils::getTimeSinceBoot() - pressedAt, rebootSent);
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
