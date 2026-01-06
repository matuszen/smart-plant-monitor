#include "NetworkTask.hpp"
#include "AppContext.hpp"

#include "Common.hpp"
#include "MQTTClient.hpp"

#include <FreeRTOS.h>
#include <hardware/gpio.h>
#include <pico/stdlib.h>
#include <projdefs.h>
#include <task.h>

#include <cstdint>
#include <cstring>

namespace
{

void updateNetworkLedState(MQTTClient& mqtt, AppContext& ctx)
{
  if (mqtt.isConnected())
  {
    ctx.setNetworkLedState(NetworkLedState::MQTT_CONNECTED);
  }
  else
  {
    const auto currentLed = ctx.readLedState().network;
    if (currentLed == NetworkLedState::MQTT_CONNECTED)
    {
      ctx.setNetworkLedState(NetworkLedState::CONNECTED);
    }
  }
}

}  // namespace

void networkTask(void* const params)
{
  auto* ctxStruct = static_cast<NetworkTaskContext*>(params);
  auto* mqtt      = ctxStruct->mqttClient;
  auto* appCtx    = ctxStruct->appContext;

  constexpr uint32_t taskTickMs = 50;

  while (true)
  {
    const auto now = Utils::getTimeSinceBoot();
    mqtt->loop(now);

    updateNetworkLedState(*mqtt, *appCtx);

    AppMessage msg{};
    if (appCtx->sensorDataQueue != nullptr)
    {
      while (xQueueReceive(appCtx->sensorDataQueue, &msg, 0) == pdPASS)
      {
        if (msg.type == AppMessage::Type::SENSOR_DATA)
        {
          mqtt->publishSensorState(now, msg.sensorData, msg.isWatering, msg.forceUpdate);
        }
        else if (msg.type == AppMessage::Type::ACTIVITY_LOG)
        {
          mqtt->publishActivity(msg.activityText.data());
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(taskTickMs));
  }
}
