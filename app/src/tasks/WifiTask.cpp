#include "WifiTask.hpp"
#include "AppContext.hpp"

#include "Common.hpp"
#include "Config.hpp"
#include "FlashManager.hpp"
#include "Types.hpp"

#include <FreeRTOS.h>
#include <hardware/gpio.h>
#include <hardware/watchdog.h>
#include <pico/stdlib.h>
#include <portmacrocommon.h>
#include <projdefs.h>
#include <task.h>

#include <cstdint>
#include <cstdio>

namespace
{

void blinkErrorAsync(const uint8_t times, const TickType_t onTicks, const TickType_t offTicks)
{
  for (uint8_t i = 0; i < times; ++i)
  {
    gpio_put(Config::LED_ERROR_PIN, true);
    vTaskDelay(onTicks);
    gpio_put(Config::LED_ERROR_PIN, false);
    vTaskDelay(offTicks);
  }
}

void processWifiCommand(const WifiCommand cmd, WifiTaskContext* const ctx, bool& connected)
{
  auto& appCtx = *ctx->appContext;
  switch (cmd)
  {
    case WifiCommand::START_PROVISIONING:
    {
      if (appCtx.apActive)
      {
        appCtx.apCancel = true;
        printf("[WiFi] AP stop requested\n");
        break;
      }

      printf("[WiFi] Button requested AP provisioning\n");
      appCtx.setNetworkLedState(NetworkLedState::PROVISIONING);
      appCtx.apCancel = false;
      appCtx.apActive = true;

      ctx->mqttClient->setWifiReady(false);

      const auto reboot = ctx->provisioner->startApAndServe(
        Config::AP::SESSION_TIMEOUT_MS, *ctx->mqttClient->getSensorController(), (bool*)&appCtx.apCancel);

      appCtx.apActive = false;

      if (reboot)
      {
        printf("[WiFi] Configuration updated, rebooting...\n");
        watchdog_reboot(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
      else
      {
        SystemConfig config;
        if (FlashManager::loadConfig(config) and config.wifi.valid)
        {
          connected = ctx->provisioner->connectSta(config.wifi);
          ctx->mqttClient->setWifiReady(connected);
          if (config.mqtt.enabled)
          {
            (void)ctx->mqttClient->init(config.mqtt);
          }
        }
        else
        {
          connected = connected and ctx->provisioner->isConnected();
          ctx->mqttClient->setWifiReady(connected);
        }
      }

      appCtx.setNetworkLedState(connected ? NetworkLedState::CONNECTED : NetworkLedState::OFF);
      break;
    }
    case WifiCommand::REBOOT:
    {
      printf("[WiFi] Reboot requested, blinking error LED 3x\n");
      blinkErrorAsync(3, pdMS_TO_TICKS(200), pdMS_TO_TICKS(200));
      watchdog_reboot(0, 0, 0);
      vTaskDelay(pdMS_TO_TICKS(100));
      break;
    }
  }
}

void handleInitialConnection(WifiTaskContext* const ctx, const SystemConfig& config, bool& connected)
{
  const auto connectedStatus = ctx->provisioner->connectSta(config.wifi);
  if (connectedStatus)
  {
    ctx->mqttClient->setWifiReady(true);
    if (config.mqtt.enabled)
    {
      (void)ctx->mqttClient->init(config.mqtt);
    }
    ctx->appContext->setNetworkLedState(NetworkLedState::CONNECTED);
    ctx->appContext->setWifiError(false);
  }
  else
  {
    ctx->mqttClient->setWifiReady(false);
    ctx->appContext->setNetworkLedState(NetworkLedState::OFF);
    printf("[WiFi] No valid connection.\n");
    ctx->appContext->setWifiError(true);
  }
  connected = connectedStatus;
}

void handleConnectionRetry(WifiTaskContext* const ctx, const SystemConfig& config, bool& connected,
                           uint32_t& lastConnectionAttempt)
{
  const auto         now                       = Utils::getTimeSinceBoot();
  constexpr uint32_t connectionRetryIntervalMs = 15'000;

  if (now - lastConnectionAttempt < connectionRetryIntervalMs)
  {
    return;
  }

  printf("[WiFi] Retrying connection...\n");
  ctx->appContext->setNetworkLedState(NetworkLedState::CONNECTING);
  connected             = ctx->provisioner->connectSta(config.wifi);
  lastConnectionAttempt = Utils::getTimeSinceBoot();

  if (connected)
  {
    ctx->mqttClient->setWifiReady(true);
    if (config.mqtt.enabled)
    {
      (void)ctx->mqttClient->init(config.mqtt);
    }
    ctx->appContext->setNetworkLedState(NetworkLedState::CONNECTED);
    ctx->appContext->setWifiError(false);
  }
  else
  {
    ctx->appContext->setNetworkLedState(NetworkLedState::OFF);
    ctx->appContext->setWifiError(true);
  }
}

}  // namespace

void wifiProvisionTask(void* const params)
{
  auto* ctx = static_cast<WifiTaskContext*>(params);
  if ((ctx == nullptr) or (ctx->provisioner == nullptr) or (ctx->mqttClient == nullptr)) [[unlikely]]
  {
    vTaskDelete(nullptr);
  }

  ctx->appContext->setNetworkLedState(NetworkLedState::CONNECTING);

  SystemConfig config;
  if (not FlashManager::loadConfig(config))
  {
    config = {};
  }

  bool connected = false;
  handleInitialConnection(ctx, config, connected);

  ctx->appContext->apActive = false;

  uint32_t lastConnectionAttempt = Utils::getTimeSinceBoot();

  while (true)
  {
    WifiCommand cmd{};
    auto* const queue = ctx->appContext->wifiCommandQueue;
    if ((queue != nullptr) and (xQueueReceive(queue, &cmd, pdMS_TO_TICKS(100)) == pdPASS))
    {
      processWifiCommand(cmd, ctx, connected);
    }

    if (not connected and not ctx->appContext->apActive and config.wifi.valid)
    {
      handleConnectionRetry(ctx, config, connected, lastConnectionAttempt);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
