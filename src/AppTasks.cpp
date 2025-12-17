#include "AppTasks.h"

#include <cstdio>

#include <FreeRTOS.h>
#include <task.h>

#include <pico/stdlib.h>
#include <pico/time.h>

#include "Config.h"
#include "Types.h"

namespace
{

constexpr UBaseType_t SENSOR_TASK_PRIORITY{tskIDLE_PRIORITY + 2};
constexpr UBaseType_t IRRIGATION_TASK_PRIORITY{tskIDLE_PRIORITY + 1};

constexpr uint16_t SENSOR_TASK_STACK{2048};
constexpr uint16_t IRRIGATION_TASK_STACK{1024};

[[nodiscard]] inline auto nowMs() -> uint32_t
{
  return to_ms_since_boot(get_absolute_time());
}

void logEnvironment(const SensorData& data)
{
  printf("  Environment: ");
  if (data.environment.isValid())
  {
    printf("Temp=%.1f°C, Humidity=%.1f%%, Pressure=%.1fhPa\n", data.environment.temperature, data.environment.humidity,
           data.environment.pressure);
  }
  else
  {
    printf("Environmental sensor not available\n");
  }

  printf("\n");
}

void logLight(const SensorData& data)
{
  printf("  Light: ");
  if (not data.light.isValid())
  {
    printf("Unavailable\n\n");
    return;
  }

  printf("%.1f lux", data.light.lux);
  if constexpr (Config::ENABLE_SERIAL_DEBUG)
  {
    printf(" (raw=%u)", data.light.rawValue);
  }
  printf("\n\n");
}

void logSoil(const SensorData& data)
{
  printf("  Soil Moisture: ");
  if (not data.soil.isValid())
  {
    printf("Error\n\n");
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
  printf(" - %s\n\n", soilStatus);
}

void logIrrigation(const IrrigationController& irrigationController)
{
  printf("  Irrigation: %s (Mode: %d)\n\n", irrigationController.isWatering() ? "ACTIVE" : "Idle",
         static_cast<int>(irrigationController.getMode()));
}

void logWaterLevel(const SensorData& data)
{
  printf("  Water Level: ");
  if (not data.water.isValid())
  {
    printf("Unavailable\n\n");
    return;
  }

  printf("%.0f%%", data.water.percentage);
  if constexpr (Config::ENABLE_SERIAL_DEBUG)
  {
    printf(" (depth=%u mm)", data.water.activeSections);
  }
  printf("\n\n");
}

void logHall(const SensorData& data)
{
  printf("  Hall Sensor: ");
  if (not data.hall.isValid())
  {
    printf("Unavailable\n\n");
    return;
  }

  printf(data.hall.isMagnetPresent() ? "Magnet detected\n\n" : "No magnetic field\n\n");
}

void handleSensorRead(uint32_t now, SensorManager& sensorManager, IrrigationController& irrigationController,
                      HomeAssistantClient& haClient)
{
  printf("[%u] Reading sensors...\n", now);

  const auto data = sensorManager.readAllSensors();

  logEnvironment(data);
  logLight(data);
  logSoil(data);
  logIrrigation(irrigationController);
  logWaterLevel(data);
  logHall(data);

  if constexpr (Config::ENABLE_HOME_ASSISTANT)
  {
    haClient.publishSensorState(now, data, irrigationController.isWatering());
  }
  printf("\n");
}

void sensorTask(void* params)
{
  auto& ctx = *static_cast<HomeAssistantClient*>(params);  // first param is haClient; we retrieve others via accessors
  auto& sensorManager        = *ctx.getSensorManager();
  auto& irrigationController = *ctx.getIrrigationController();

  uint32_t           lastSensorRead{0};
  constexpr uint32_t sensorTaskTickMs{100};
  while (true)
  {
    const auto now = nowMs();
    if constexpr (Config::ENABLE_HOME_ASSISTANT)
    {
      ctx.loop(now);
    }

    if (now - lastSensorRead >= Config::SENSOR_READ_INTERVAL_MS)
    {
      handleSensorRead(now, sensorManager, irrigationController, ctx);
      lastSensorRead = now;
    }

    vTaskDelay(pdMS_TO_TICKS(sensorTaskTickMs));
  }
}

void irrigationTask(void* params)
{
  auto&              irrigationController = *static_cast<IrrigationController*>(params);
  constexpr uint32_t irrigationTickMs{200};
  while (true)
  {
    irrigationController.update();
    vTaskDelay(pdMS_TO_TICKS(irrigationTickMs));
  }
}

}  // namespace

void startAppTasks(SensorManager& sensorManager, IrrigationController& irrigationController,
                   HomeAssistantClient& haClient)
{
  // Wire back-pointers so tasks can access shared objects without globals
  haClient.setControllers(&sensorManager, &irrigationController);

  xTaskCreate(sensorTask, "sensorTask", SENSOR_TASK_STACK, &haClient, SENSOR_TASK_PRIORITY, nullptr);
  xTaskCreate(irrigationTask, "irrigationTask", IRRIGATION_TASK_STACK, &irrigationController, IRRIGATION_TASK_PRIORITY,
              nullptr);

  vTaskStartScheduler();
}
