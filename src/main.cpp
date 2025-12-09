#include <cstdint>
#include <cstdio>

#include <hardware/gpio.h>
#include <pico/stdio.h>
#include <pico/time.h>

#include "Config.h"
#include "DataLogger.h"
#include "HomeAssistantClient.h"
#include "IrrigationController.h"
#include "SensorManager.h"
#include "Types.h"

namespace
{

SensorManager        sensorManager;
IrrigationController irrigationController(&sensorManager);
DataLogger           dataLogger;
HomeAssistantClient  haClient(&sensorManager, &irrigationController);

uint32_t lastSensorReadTime = 0;
uint32_t lastLEDToggleTime  = 0;
bool     currentLedState    = false;

void initSystem()
{
  printf("\n");
  printf("=================================================\n");
  printf("  %.*s v%.*s\n", static_cast<int>(Config::SYSTEM_NAME.size()), Config::SYSTEM_NAME.data(),
         static_cast<int>(Config::SYSTEM_VERSION.size()), Config::SYSTEM_VERSION.data());
  printf("  Raspberry Pi Pico - Plant Monitoring System\n");
  printf("=================================================\n\n");

  gpio_init(Config::STATUS_LED_PIN);
  gpio_set_dir(Config::STATUS_LED_PIN, GPIO_OUT);
  gpio_put(Config::STATUS_LED_PIN, true);

  if (not sensorManager.init())
  {
    printf("ERROR: SensorManager initialization failed!\n");
    while (true)
    {
      gpio_put(Config::STATUS_LED_PIN, true);
      sleep_ms(200);
      gpio_put(Config::STATUS_LED_PIN, false);
      sleep_ms(200);
    }
  }

  if (not irrigationController.init())
  {
    printf("ERROR: IrrigationController initialization failed!\n");
    while (true)
    {
      gpio_put(Config::STATUS_LED_PIN, true);
      sleep_ms(200);
      gpio_put(Config::STATUS_LED_PIN, false);
      sleep_ms(200);
    }
  }

  if (not dataLogger.init())
  {
    printf("ERROR: DataLogger initialization failed!\n");
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
  printf("- BME280 (I2C0): GP%d (SDA), GP%d (SCL)\n", Config::BME280_SDA_PIN, Config::BME280_SCL_PIN);
  printf("- Light (BH1750) on I2C%u: GP%d (SDA), GP%d (SCL) addr 0x%02X\n", Config::LIGHT_SENSOR_I2C_INSTANCE,
         Config::LIGHT_SENSOR_SDA_PIN, Config::LIGHT_SENSOR_SCL_PIN, Config::LIGHT_SENSOR_I2C_ADDRESS);
  printf("- Soil Moisture: GP%d (ADC%d)\n", Config::SOIL_MOISTURE_ADC_PIN, Config::SOIL_MOISTURE_ADC_CHANNEL);
  printf("- Water Level: Grove sensor on I2C%u (addr 0x%02X/0x%02X)\n", Config::WATER_LEVEL_I2C_INSTANCE,
         Config::WATER_LEVEL_LOW_ADDR, Config::WATER_LEVEL_HIGH_ADDR);
  printf("- Relay (Pump): GP%d\n", Config::RELAY_PIN);
  printf("- Status LED: GP%d\n", Config::STATUS_LED_PIN);
  printf("=================================================\n\n");

  printf("System ready! Starting main loop...\n\n");
  gpio_put(Config::STATUS_LED_PIN, false);
}

void mainLoop()
{
  const auto now = to_ms_since_boot(get_absolute_time());

  if constexpr (Config::ENABLE_HOME_ASSISTANT)
  {
    haClient.loop(now);
  }

  irrigationController.update();

  if (now - lastLEDToggleTime >= Config::STATUS_LED_BLINK_MS)
  {
    currentLedState = not currentLedState;
    gpio_put(Config::STATUS_LED_PIN, currentLedState);
    lastLEDToggleTime = now;
  }

  if (now - lastSensorReadTime >= Config::SENSOR_READ_INTERVAL_MS)
  {
    printf("[%u] Reading sensors...\n", now);

    const auto data = sensorManager.readAllSensors();

    printf("  Environment: ");
    if (data.environment.isValid())
    {
      printf("Temp=%.1f°C, Humidity=%.1f%%, Pressure=%.1fhPa\n", data.environment.temperature,
             data.environment.humidity, data.environment.pressure);
    }
    else
    {
      printf("BME280 not available\n");
    }

    printf("  Light: ");
    if (data.lightLevelAvailable)
    {
      if (data.light.isValid())
      {
        printf("%.1f lux\n", data.light.lux);
      }
      else
      {
        printf("Unavailable\n");
      }
    }
    else
    {
      printf("Disabled\n");
    }

    printf("  Soil Moisture: ");
    if (data.soil.isValid())
    {
      const auto* soilStatus = "OK";
      if (data.soil.isDry())
      {
        soilStatus = "DRY";
      }
      else if (data.soil.isWet())
      {
        soilStatus = "WET";
      }
      printf("%.1f%% (raw=%u) - %s\n", data.soil.percentage, data.soil.rawValue, soilStatus);
    }
    else
    {
      printf("Error\n");
    }

    printf("  Irrigation: %s (Mode: %d)\n", irrigationController.isWatering() ? "ACTIVE" : "Idle",
           static_cast<int>(irrigationController.getMode()));

    printf("  Water Level: ");
    if (data.waterLevelAvailable)
    {
      if (data.water.isValid())
      {
        printf("%.0f%% (depth=%umm)\n", data.water.percentage, data.water.rawValue);
      }
      else
      {
        printf("Unavailable\n");
      }
    }
    else
    {
      printf("Disabled\n");
    }

    dataLogger.logData(data, irrigationController.isWatering());

    if constexpr (Config::ENABLE_HOME_ASSISTANT)
    {
      haClient.publishSensorState(now, data, irrigationController.isWatering());
    }

    lastSensorReadTime = now;
    printf("\n");
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
