#include "SensorTask.hpp"
#include "AppContext.hpp"
#include "IrrigationController.hpp"
#include "SensorController.hpp"

#include "Common.hpp"
#include "Config.hpp"
#include "FlashManager.hpp"
#include "MQTTClient.hpp"
#include "Types.hpp"

#include <FreeRTOS.h>
#include <pico/stdlib.h>
#include <pico/time.h>
#include <projdefs.h>
#include <task.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{

void logEnvironment(const SensorData& data)
{
  printf("  Environment: ");
  if (not data.environment.isValid()) [[unlikely]]
  {
    printf("Unavailable\n");
    return;
  }
  printf("Temp=%.1f°C, Humidity=%.1f%%, Pressure=%.1fhPa\n", data.environment.temperature, data.environment.humidity,
         data.environment.pressure);
}

void logLight(const SensorData& data)
{
  printf("  Light: ");
  if (not data.light.isValid()) [[unlikely]]
  {
    printf("Unavailable\n");
    return;
  }

  printf("%.1f lux", data.light.lux);
  if constexpr (Config::ENABLE_SERIAL_DEBUG)
  {
    printf(" (raw=%u)", data.light.rawValue);
  }
  printf("\n");
}

void logSoil(const SensorData& data)
{
  printf("  Soil Moisture: ");
  if (not data.soil.isValid()) [[unlikely]]
  {
    printf("Error\n");
    return;
  }

  const auto* soilStatus = "OK";
  if (data.soil.isDry())
  {
    soilStatus = "DRY";
  }
  else if (data.soil.isWet())
  {
    soilStatus = "WET";
  }

  printf("%.1f%%", data.soil.percentage);
  if constexpr (Config::ENABLE_SERIAL_DEBUG)
  {
    printf(" (raw=%u)", data.soil.rawValue);
  }
  printf(" - %s\n", soilStatus);
}

void logIrrigation(const IrrigationController& irrigationController)
{
  printf("  Irrigation: %s (Mode: %d)\n", irrigationController.isWatering() ? "ACTIVE" : "Idle",
         static_cast<int>(irrigationController.getMode()));
}

void logWaterLevel(const SensorData& data)
{
  printf("  Water Level: ");
  if (not data.water.isValid()) [[unlikely]]
  {
    printf("Unavailable\n");
    return;
  }

  printf("%.0f%%\n", data.water.percentage);
}

void updateErrorLedFromData(AppContext& ctx, const SensorData& data)
{
  const auto waterLow   = data.water.isValid() and data.water.isLow();
  const auto sensorsBad = (not data.environment.isValid()) or (not data.soil.isValid()) or (not data.water.isValid());
  ctx.setSensorError(waterLow or sensorsBad);
}

auto handleSensorRead(const uint32_t now, SensorController& sensorController,
                      IrrigationController& irrigationController, AppContext& ctx,
                      const bool force = false) -> SensorData
{
  printf("[%u] Reading sensors...\n", now);

  const auto data = sensorController.readAllSensors();

  logEnvironment(data);
  logLight(data);
  logSoil(data);
  logIrrigation(irrigationController);
  logWaterLevel(data);
  updateErrorLedFromData(ctx, data);

  irrigationController.update(data);

  const auto msg = AppMessage{
    .type        = AppMessage::Type::SENSOR_DATA,
    .sensorData  = data,
    .isWatering  = irrigationController.isWatering(),
    .forceUpdate = force,
  };

  if (ctx.sensorDataQueue != nullptr)
  {
    xQueueSend(ctx.sensorDataQueue, &msg, 0);
  }

  return data;
}

void handleWaterLevelError(const uint32_t now, uint32_t& lastSensorRead, bool& waterLevelError,
                           SensorController& sensorController, IrrigationController& irrigationController,
                           AppContext& ctx)
{
  const auto waterData = sensorController.readWaterLevel();
  if (waterData.isValid() and not waterData.isLow())
  {
    waterLevelError = false;
    const auto data = handleSensorRead(now, sensorController, irrigationController, ctx);
    waterLevelError = (data.water.isValid() and data.water.isLow());
  }
  else
  {
    SensorData dummyData{};
    dummyData.water = waterData;
    logWaterLevel(dummyData);
    updateErrorLedFromData(ctx, dummyData);
  }
  lastSensorRead = now;
}

void handleNormalSensorRead(const uint32_t now, uint32_t& lastSensorRead, bool& waterLevelError,
                            SensorController& sensorController, IrrigationController& irrigationController,
                            AppContext& ctx, bool force = false)
{
  const auto data = handleSensorRead(now, sensorController, irrigationController, ctx, force);
  waterLevelError = (data.water.isValid() and data.water.isLow());
  lastSensorRead  = now;
}

auto shouldPerformSensorRead(const uint32_t now, const uint32_t lastSensorRead, const uint32_t sensorReadInterval,
                             const bool waterLevelError) -> bool
{
  if (waterLevelError)
  {
    constexpr uint32_t errorRetryIntervalMs = 15'000;
    return (now - lastSensorRead >= errorRetryIntervalMs);
  }
  return (now - lastSensorRead >= sensorReadInterval);
}

void handleWateringStateChange(bool& wasWatering, const bool isWatering, const uint32_t now,
                               uint32_t& scheduledReadTime, bool& pendingPostWateringRead, AppContext& ctx)
{
  if (wasWatering and not isWatering)
  {
    AppMessage msg;
    msg.type = AppMessage::Type::ACTIVITY_LOG;
    strncpy(msg.activityText.data(), "Irrigation finished", msg.activityText.size() - 1);
    msg.activityText[msg.activityText.size() - 1] = '\0';
    if (ctx.sensorDataQueue)
    {
      xQueueSend(ctx.sensorDataQueue, &msg, 0);
    }

    scheduledReadTime       = now + 60'000;
    pendingPostWateringRead = true;
  }
  else if (not wasWatering and isWatering)
  {
    AppMessage msg;
    msg.type = AppMessage::Type::ACTIVITY_LOG;
    strncpy(msg.activityText.data(), "Irrigation started", msg.activityText.size() - 1);
    msg.activityText[msg.activityText.size() - 1] = '\0';
    if (ctx.sensorDataQueue)
    {
      xQueueSend(ctx.sensorDataQueue, &msg, 0);
    }
  }
  wasWatering = isWatering;
}

void determineSensorReadNeeds(const uint32_t now, const uint32_t lastSensorRead, const uint32_t sensorReadInterval,
                              const bool waterLevelError, bool& shouldRead, bool& onlyWaterLevel, bool& forceUpdate,
                              const uint32_t scheduledReadTime, bool& pendingPostWateringRead,
                              IrrigationController& irrigationController, MQTTClient& mqttClient)
{
  if (mqttClient.isUpdateRequested())
  {
    shouldRead  = true;
    forceUpdate = true;
    mqttClient.clearUpdateRequest();
  }
  else
  {
    const bool isManualMode = (irrigationController.getMode() == IrrigationMode::MANUAL);
    if (not isManualMode)
    {
      shouldRead     = shouldPerformSensorRead(now, lastSensorRead, sensorReadInterval, waterLevelError);
      onlyWaterLevel = waterLevelError and (now - lastSensorRead >= 15'000);
    }

    if (pendingPostWateringRead and now >= scheduledReadTime)
    {
      shouldRead              = true;
      onlyWaterLevel          = false;
      pendingPostWateringRead = false;
    }
  }
}

}  // namespace

void sensorTask(void* const params)
{
  auto& taskCtx              = *static_cast<SensorTaskContext*>(params);
  auto& appCtx               = *taskCtx.appContext;
  auto& sensorController     = *taskCtx.sensorController;
  auto& irrigationController = *taskCtx.irrigationController;
  auto& mqttClient           = *taskCtx.mqttClient;

  SystemConfig config;
  if (not FlashManager::loadConfig(config)) [[unlikely]]
  {
    config = {};
  }

  auto sensorReadInterval = config.sensorReadIntervalMs;
  if (sensorReadInterval == 0)
  {
    sensorReadInterval = Config::DEFAULT_SENSOR_READ_INTERVAL_MS;
  }

  auto               lastSensorRead   = sensorReadInterval;
  constexpr uint32_t sensorTaskTickMs = 100;

  bool     wasWatering             = false;
  uint32_t scheduledReadTime       = 0;
  bool     pendingPostWateringRead = false;
  bool     waterLevelError         = false;

  while (true)
  {
    const auto now        = Utils::getTimeSinceBoot();
    const auto isWatering = irrigationController.isWatering();
    appCtx.setActivityLedState(isWatering);

    handleWateringStateChange(wasWatering, isWatering, now, scheduledReadTime, pendingPostWateringRead, appCtx);

    bool shouldRead     = false;
    bool onlyWaterLevel = false;
    bool forceUpdate    = false;

    determineSensorReadNeeds(now, lastSensorRead, sensorReadInterval, waterLevelError, shouldRead, onlyWaterLevel,
                             forceUpdate, scheduledReadTime, pendingPostWateringRead, irrigationController, mqttClient);

    if (shouldRead)
    {
      appCtx.setActivityLedState(true);

      if (onlyWaterLevel)
      {
        handleWaterLevelError(now, lastSensorRead, waterLevelError, sensorController, irrigationController, appCtx);
      }
      else
      {
        handleNormalSensorRead(now, lastSensorRead, waterLevelError, sensorController, irrigationController, appCtx,
                               forceUpdate);
      }

      appCtx.setActivityLedState(irrigationController.isWatering());
    }

    vTaskDelay(pdMS_TO_TICKS(sensorTaskTickMs));
  }
}
