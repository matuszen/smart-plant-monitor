#include <cstdint>
#include <cstdio>

#include <hardware/gpio.h>
#include <pico/stdio.h>
#include <pico/time.h>

#include "Config.h"
#include "DataLogger.h"
#include "IrrigationController.h"
#include "SensorManager.h"
#include "Types.h"

namespace
{

SensorManager        sensorManager;
IrrigationController irrigationController(&sensorManager);
DataLogger           dataLogger;

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
  gpio_set_dir(Config::STATUS_LED_PIN, false);
  gpio_put(Config::STATUS_LED_PIN, true);

  if (not sensorManager.init())
  {
    printf("ERROR: SensorManager initialization failed!\n");
    while (true)
    {
      gpio_put(Config::STATUS_LED_PIN, true);
      sleep_ms(100);
      gpio_put(Config::STATUS_LED_PIN, false);
      sleep_ms(100);
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

  printf("\n=================================================\n");
  printf("System Configuration:\n");
  printf("- BME280 (I2C0): GP%d (SDA), GP%d (SCL)\n", Config::BME280_SDA_PIN,
         Config::BME280_SCL_PIN);
  printf("- Soil Moisture: GP%d (ADC%d)\n", Config::SOIL_MOISTURE_PIN, Config::SOIL_MOISTURE_ADC);
  printf("- Water Level: GP%d (ADC%d), Power: GP%d\n", Config::WATER_LEVEL_SIGNAL_PIN,
         Config::WATER_LEVEL_ADC, Config::WATER_LEVEL_POWER_PIN);
  printf("- Relay (Pump): GP%d\n", Config::RELAY_PIN);
  printf("- Status LED: GP%d\n", Config::STATUS_LED_PIN);
  printf("=================================================\n\n");

  printf("System ready! Starting main loop...\n\n");
  gpio_put(Config::STATUS_LED_PIN, false);
}

void mainLoop()
{
  const auto now = to_ms_since_boot(get_absolute_time());

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

    printf("  Water Level: ");
    if (data.water.isValid())
    {
      const auto* statusMessage = "OK";
      if (data.water.isEmpty())
      {
        statusMessage = "EMPTY!";
      }
      else if (data.water.isLow())
      {
        statusMessage = "LOW";
      }
      printf("%.1f%% (raw=%u) - %s\n", data.water.percentage, data.water.rawValue, statusMessage);
    }
    else
    {
      printf("Error\n");
    }

    printf("  Irrigation: %s (Mode: %d)\n", irrigationController.isWatering() ? "ACTIVE" : "Idle",
           static_cast<int>(irrigationController.getMode()));

    dataLogger.logData(data, irrigationController.isWatering());

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
