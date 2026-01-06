#include "TaskEntry.hpp"
#include "AppContext.hpp"
#include "ButtonTask.hpp"
#include "ConnectionController.hpp"
#include "IrrigationController.hpp"
#include "LedTask.hpp"
#include "NetworkTask.hpp"
#include "SensorController.hpp"
#include "SensorTask.hpp"
#include "TaskConfig.hpp"
#include "WifiTask.hpp"

#include "Config.hpp"
#include "MQTTClient.hpp"

#include <FreeRTOS.h>
#include <hardware/gpio.h>
#include <pico/time.h>
#include <projdefs.h>
#include <task.h>

#include <cstdint>
#include <cstdio>

namespace
{

void initUserInterfacePins()
{
  gpio_init(Config::LED_STATUS_PIN);
  gpio_init(Config::LED_NETWORK_PIN);
  gpio_init(Config::LED_ERROR_PIN);

  gpio_set_dir(Config::LED_STATUS_PIN, GPIO_OUT);
  gpio_set_dir(Config::LED_NETWORK_PIN, GPIO_OUT);
  gpio_set_dir(Config::LED_ERROR_PIN, GPIO_OUT);

  gpio_put(Config::LED_STATUS_PIN, false);
  gpio_put(Config::LED_NETWORK_PIN, false);
  gpio_put(Config::LED_ERROR_PIN, false);

  gpio_init(Config::BUTTON_PIN);
  gpio_set_dir(Config::BUTTON_PIN, GPIO_IN);
  gpio_pull_down(Config::BUTTON_PIN);
}

void blinkErrorBlocking(const uint8_t times, const uint32_t onMs = 150, const uint32_t offMs = 150)
{
  for (uint8_t i = 0; i < times; ++i)
  {
    gpio_put(Config::LED_ERROR_PIN, true);
    sleep_ms(onMs);
    gpio_put(Config::LED_ERROR_PIN, false);
    sleep_ms(offMs);
  }
}

void irrigationTask(void* const params)
{
  auto& irrigationController = *static_cast<IrrigationController*>(params);
  while (true)
  {
    irrigationController.checkWateringTimeout();
    const auto sleepMs = irrigationController.nextSleepHintMs();
    vTaskDelay(pdMS_TO_TICKS(sleepMs));
  }
}

}  // namespace

void startAppTasks(IrrigationController& irrigationController, MQTTClient& mqttClient,
                   ConnectionController& provisioner)
{
  initUserInterfacePins();

  blinkErrorBlocking(3);

  static AppContext appContext{};

  appContext.ledStateMutex    = xSemaphoreCreateMutex();
  appContext.wifiCommandQueue = xQueueCreate(2, sizeof(WifiCommand));
  appContext.sensorDataQueue  = xQueueCreate(5, sizeof(AppMessage));

  if ((appContext.ledStateMutex == nullptr) or (appContext.wifiCommandQueue == nullptr) or
      (appContext.sensorDataQueue == nullptr)) [[unlikely]]
  {
    printf("[AppTasks] Failed to create synchronization primitives\n");
  }

  static auto wifiCtx = WifiTaskContext{
    .provisioner = &provisioner,
    .mqttClient  = &mqttClient,
    .appContext  = &appContext,
  };

  static auto networkCtx = NetworkTaskContext{
    .mqttClient = &mqttClient,
    .appContext = &appContext,
  };

  static auto sensorCtx = SensorTaskContext{
    .sensorController     = mqttClient.getSensorController(),
    .irrigationController = &irrigationController,
    .appContext           = &appContext,
    .mqttClient           = &mqttClient,
  };

  xTaskCreate(wifiProvisionTask, "wifiProv", WIFI_PROV_STACK, &wifiCtx, WIFI_PROV_PRIORITY, nullptr);

  xTaskCreate(buttonTask, "button", BUTTON_TASK_STACK, &appContext, BUTTON_TASK_PRIORITY, nullptr);
  xTaskCreate(ledTask, "leds", LED_TASK_STACK, &appContext, LED_TASK_PRIORITY, nullptr);

  xTaskCreate(sensorTask, "sensorTask", SENSOR_TASK_STACK, &sensorCtx, SENSOR_TASK_PRIORITY, nullptr);
  xTaskCreate(networkTask, "networkTask", NETWORK_TASK_STACK, &networkCtx, NETWORK_TASK_PRIORITY, nullptr);

  xTaskCreate(irrigationTask, "irrigationTask", IRRIGATION_TASK_STACK, &irrigationController, IRRIGATION_TASK_PRIORITY,
              nullptr);

  printf("[AppTasks] Starting FreeRTOS scheduler\n");
  vTaskStartScheduler();
}
