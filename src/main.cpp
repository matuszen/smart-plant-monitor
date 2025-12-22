#include <cstdio>

#include <FreeRTOS.h>
#include <task.h>

#include <hardware/gpio.h>
#include <pico/platform/common.h>
#include <pico/platform/panic.h>
#include <pico/stdio.h>
#include <pico/stdlib.h>
#include <pico/time.h>

#include "AppTasks.h"
#include "Config.h"
#include "HomeAssistantClient.h"
#include "IrrigationController.h"
#include "SensorManager.h"
#include "WifiProvisioner.h"

namespace
{

SensorManager        sensorManager;
IrrigationController irrigationController(&sensorManager);
HomeAssistantClient  haClient(&sensorManager, &irrigationController);
WifiProvisioner      wifiProvisioner;

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

  printf("\n=================================================\n");
  printf("System Configuration:\n");
  printf("- Environmental Sensor (BME280): GP%d (SDA), GP%d (SCL)\n", Config::BME280_SDA_PIN, Config::BME280_SCL_PIN);
  printf("- Light (BH1750) on I2C%u: GP%d (SDA), GP%d (SCL) addr 0x%02X\n", Config::LIGHT_SENSOR_I2C_INSTANCE,
         Config::LIGHT_SENSOR_SDA_PIN, Config::LIGHT_SENSOR_SCL_PIN, Config::LIGHT_SENSOR_I2C_ADDRESS);
  printf("- Soil Moisture: GP%d (ADC%d)\n", Config::SOIL_MOISTURE_ADC_PIN, Config::SOIL_MOISTURE_ADC_CHANNEL);
  printf("- Water Level: Grove sensor on I2C%u (addr 0x%02X/0x%02X)\n", Config::WATER_LEVEL_I2C_INSTANCE,
         Config::WATER_LEVEL_LOW_ADDR, Config::WATER_LEVEL_HIGH_ADDR);
  printf("- Pump control: GP%d\n", Config::PUMP_CONTROL_PIN);
  printf("=================================================\n\n");

  printf("System ready! Starting FreeRTOS tasks...\n\n");
}

}  // namespace

auto main() -> int
{
  stdio_init_all();
  sleep_ms(5'000);

  initSystem();

  startAppTasks(sensorManager, irrigationController, haClient, wifiProvisioner);

  while (true)
  {
    tight_loop_contents();
  }

  return 0;
}

extern "C"
{
  void vApplicationMallocFailedHook(void)
  {
    panic("FreeRTOS: Malloc Failed! Brak pamieci na stercie.");
  }

  void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName)
  {
    (void)xTask;
    panic("FreeRTOS: Stack Overflow w zadaniu: %s", pcTaskName);
  }

  void vApplicationIdleHook(void)
  {
  }

  void vApplicationTickHook(void)
  {
  }
}
