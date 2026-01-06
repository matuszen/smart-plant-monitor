#include "ConnectionController.hpp"
#include "IrrigationController.hpp"
#include "SensorController.hpp"
#include "TaskEntry.hpp"

#include "Config.hpp"
#include "FlashManager.hpp"
#include "MQTTClient.hpp"
#include "Types.hpp"

#include <FreeRTOS.h>
#include <hardware/gpio.h>
#include <pico/platform/common.h>
#include <pico/platform/panic.h>
#include <pico/stdio.h>
#include <pico/stdlib.h>
#include <pico/time.h>
#include <task.h>

#include <cstdint>
#include <cstdio>

namespace
{

SensorController     sensorController;
IrrigationController irrigationController(sensorController);
MQTTClient           mqttClient(sensorController, irrigationController);
ConnectionController connectionController;

void initSystem()
{
  printf("\n");
  printf("=================================================\n");
  printf("  %.*s v%.*s\n", static_cast<int32_t>(Config::System::NAME.size()), Config::System::NAME.data(),
         static_cast<int32_t>(Config::System::VERSION.size()), Config::System::VERSION.data());
  printf("=================================================\n\n");

  if (not sensorController.init()) [[unlikely]]
  {
    printf("ERROR: SensorController initialization failed!\n");
  }

  if (not irrigationController.init()) [[unlikely]]
  {
    printf("ERROR: IrrigationController initialization failed!\n");
  }

  SystemConfig config;
  if (FlashManager::loadConfig(config)) [[likely]]
  {
    irrigationController.setMode(config.irrigationMode);
    printf("Configuration loaded. Irrigation Mode: %d\n", static_cast<int32_t>(config.irrigationMode));
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

  printf("System ready! Starting tasks...\n\n");
}

}  // namespace

auto main(const int /*argc*/, const char* const /*argv*/[]) -> int
{
  stdio_init_all();
  if constexpr (Config::ENABLE_SERIAL_DEBUG)
  {
    sleep_ms(Config::INITIAL_DELAY_MS);
  }

  initSystem();
  startAppTasks(irrigationController, mqttClient, connectionController);

  while (true)
  {
    tight_loop_contents();
  }

  return 0;
}
