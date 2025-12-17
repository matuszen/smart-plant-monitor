#include <cstdint>
#include <cstdio>

#include <hardware/gpio.h>
#include <pico/stdio.h>
#include <pico/time.h>

#include "Config.h"
#include "HomeAssistantClient.h"
#include "IrrigationController.h"
#include "SensorManager.h"
#include "Types.h"

namespace
{

SensorManager        sensorManager;
IrrigationController irrigationController(&sensorManager);
HomeAssistantClient  haClient(&sensorManager, &irrigationController);

uint64_t lastSensorReadTime = 0;
uint64_t lastLEDToggleTime  = 0;
bool     currentLedState    = false;

[[nodiscard]] inline auto nowMs() -> uint32_t
{
  return to_ms_since_boot(get_absolute_time());
}

void initSystem()
{
  printf("\n");
  printf("=================================================\n");
  printf("  %.*s v%.*s\n", static_cast<int>(Config::SYSTEM_NAME.size()), Config::SYSTEM_NAME.data(),
         static_cast<int>(Config::SYSTEM_VERSION.size()), Config::SYSTEM_VERSION.data());
  printf("=================================================\n\n");

  if (not sensorManager.init())
  {
    printf("ERROR: SensorManager initialization failed!\n");
  }

  if (not irrigationController.init())
  {
    printf("ERROR: IrrigationController initialization failed!\n");
  }

  if constexpr (Config::ENABLE_HOME_ASSISTANT)
  {
    if (not haClient.init())
    {
      printf("WARNING: Home Assistant integration failed to initialize (Wi-Fi/MQTT)\n");
    }
  }

  printf("\n=================================================\n");
  printf("System Configuration:\n");
  printf("- Environmental Sensor (BME280): GP%d (SDA), GP%d (SCL)\n", Config::BME280_SDA_PIN, Config::BME280_SCL_PIN);
  printf("- Light (BH1750) on I2C%u: GP%d (SDA), GP%d (SCL) addr 0x%02X\n", Config::LIGHT_SENSOR_I2C_INSTANCE,
         Config::LIGHT_SENSOR_SDA_PIN, Config::LIGHT_SENSOR_SCL_PIN, Config::LIGHT_SENSOR_I2C_ADDRESS);
  printf("- Soil Moisture: GP%d (ADC%d)\n", Config::SOIL_MOISTURE_ADC_PIN, Config::SOIL_MOISTURE_ADC_CHANNEL);
  printf("- Water Level: Grove sensor on I2C%u (addr 0x%02X/0x%02X)\n", Config::WATER_LEVEL_I2C_INSTANCE,
         Config::WATER_LEVEL_LOW_ADDR, Config::WATER_LEVEL_HIGH_ADDR);
  printf("- Relay (Pump): GP%d\n", Config::RELAY_PIN);
  printf("=================================================\n\n");

  printf("System ready! Starting main loop...\n\n");
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

void logIrrigation()
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

void handleSensorRead(uint32_t now)
{
  printf("[%u] Reading sensors...\n", now);

  const auto data = sensorManager.readAllSensors();

  logEnvironment(data);
  logLight(data);
  logSoil(data);
  logIrrigation();
  logWaterLevel(data);

  if constexpr (Config::ENABLE_HOME_ASSISTANT)
  {
    haClient.publishSensorState(now, data, irrigationController.isWatering());
  }

  lastSensorReadTime = now;
  printf("\n");
}

void mainLoop()
{
  const auto now = nowMs();

  if constexpr (Config::ENABLE_HOME_ASSISTANT)
  {
    haClient.loop(static_cast<uint32_t>(now));
  }

  irrigationController.update();

  if (now - lastSensorReadTime >= Config::SENSOR_READ_INTERVAL_MS)
  {
    handleSensorRead(now);
  }

  sleep_ms(100);
}

}  // namespace

auto main() -> int
{
  stdio_init_all();
  sleep_ms(5'000);

  initSystem();

  while (true)
  {
    mainLoop();
  }

  return 0;
}
