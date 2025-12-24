#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <FreeRTOS.h>
#include <projdefs.h>
#include <queue.h>
#include <task.h>

#include <hardware/gpio.h>
#include <hardware/watchdog.h>
#include <pico/cyw43_arch.h>
#include <pico/platform/panic.h>
#include <pico/stdlib.h>
#include <pico/time.h>

#include "AppTasks.h"
#include "Config.h"
#include "HomeAssistantClient.h"
#include "IrrigationController.h"
#include "SensorManager.h"
#include "Types.h"
#include "WifiProvisioner.h"

#include "portmacrocommon.h"

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

namespace
{

inline constexpr UBaseType_t SENSOR_TASK_PRIORITY{tskIDLE_PRIORITY + 2};
inline constexpr UBaseType_t IRRIGATION_TASK_PRIORITY{tskIDLE_PRIORITY + 1};
inline constexpr UBaseType_t WIFI_PROV_PRIORITY{tskIDLE_PRIORITY + 1};
inline constexpr UBaseType_t BUTTON_TASK_PRIORITY{tskIDLE_PRIORITY + 4};
inline constexpr UBaseType_t LED_TASK_PRIORITY{tskIDLE_PRIORITY + 1};

inline constexpr uint16_t SENSOR_TASK_STACK{2048};
inline constexpr uint16_t IRRIGATION_TASK_STACK{1024};
inline constexpr uint16_t WIFI_PROV_STACK{2048};
inline constexpr uint16_t BUTTON_TASK_STACK{768};
inline constexpr uint16_t LED_TASK_STACK{768};

enum class WifiCommand : uint8_t
{
  START_PROVISIONING = 0,
  REBOOT             = 1
};

enum class NetworkLedState : uint8_t
{
  OFF,
  CONNECTING,
  PROVISIONING,
  CONNECTED
};

struct LedSharedState
{
  bool            errorOn{false};
  NetworkLedState network{NetworkLedState::OFF};
};

QueueHandle_t     wifiCommandQueue{nullptr};
SemaphoreHandle_t ledStateMutex{nullptr};
LedSharedState    ledShared{};
volatile bool     apActiveFlag{false};
volatile bool     apCancelFlag{false};

[[nodiscard]] inline auto nowMs() -> uint32_t
{
  return to_ms_since_boot(get_absolute_time());
}

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

void setNetworkLedState(const NetworkLedState state)
{
  if (ledStateMutex == nullptr) [[unlikely]]
  {
    return;
  }
  xSemaphoreTake(ledStateMutex, portMAX_DELAY);
  ledShared.network = state;
  xSemaphoreGive(ledStateMutex);
}

void setErrorLedState(const bool on)
{
  if (ledStateMutex == nullptr) [[unlikely]]
  {
    return;
  }
  xSemaphoreTake(ledStateMutex, portMAX_DELAY);
  ledShared.errorOn = on;
  xSemaphoreGive(ledStateMutex);
}

[[nodiscard]] auto readLedState() -> LedSharedState
{
  if (ledStateMutex == nullptr) [[unlikely]]
  {
    return LedSharedState{};
  }

  xSemaphoreTake(ledStateMutex, portMAX_DELAY);
  const auto snapshot = ledShared;
  xSemaphoreGive(ledStateMutex);
  return snapshot;
}

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
  if (not data.water.isValid())
  {
    printf("Unavailable\n");
    return;
  }

  printf("%.0f%%\n", data.water.percentage);
}

void updateErrorLedFromData(const SensorData& data)
{
  const auto waterLow   = data.water.isValid() and data.water.isLow();
  const auto sensorsBad = (not data.environment.isValid()) or (not data.soil.isValid()) or (not data.water.isValid());
  setErrorLedState(waterLow or sensorsBad);
}

void ledTask(void* /*params*/)
{
  uint32_t lastStatusToggle{0};
  uint32_t lastNetworkToggle{0};
  bool     statusOn{false};
  bool     networkOn{false};

  constexpr uint32_t statusPeriodMs{500};
  constexpr uint32_t connectBlinkMs{400};
  constexpr uint32_t provisionBlinkMs{150};

  while (true)
  {
    const auto now = nowMs();
    auto       led = readLedState();

    if ((now - lastStatusToggle) >= statusPeriodMs)
    {
      statusOn         = not statusOn;
      lastStatusToggle = now;
    }
    gpio_put(Config::LED_STATUS_PIN, statusOn);

    switch (led.network)
    {
      case NetworkLedState::CONNECTED:
        networkOn         = true;
        lastNetworkToggle = now;
        break;
      case NetworkLedState::PROVISIONING:
        if ((now - lastNetworkToggle) >= provisionBlinkMs)
        {
          networkOn         = not networkOn;
          lastNetworkToggle = now;
        }
        break;
      case NetworkLedState::CONNECTING:
        if ((now - lastNetworkToggle) >= connectBlinkMs)
        {
          networkOn         = not networkOn;
          lastNetworkToggle = now;
        }
        break;
      case NetworkLedState::OFF:
      default:
        networkOn = false;
        break;
    }
    gpio_put(Config::LED_NETWORK_PIN, networkOn);

    gpio_put(Config::LED_ERROR_PIN, led.errorOn);

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void handleButtonRelease(uint32_t heldMs, bool& rebootSent)
{
  if (rebootSent)
  {
    rebootSent = false;
    return;
  }

  printf("[Button] Released after %u ms\n", heldMs);

  if (heldMs >= Config::BUTTON_AP_MIN_MS and heldMs < Config::BUTTON_REBOOT_MS)
  {
    const WifiCommand cmd = WifiCommand::START_PROVISIONING;
    if (wifiCommandQueue != nullptr)
    {
      xQueueSend(wifiCommandQueue, &cmd, 0);
      if (apActiveFlag)
      {
        apCancelFlag = true;
      }
      printf("[Button] AP toggle requested after %u ms hold\n", heldMs);
    }
  }
}

void handleButtonHold(uint32_t heldMs, bool& rebootSent)
{
  if (heldMs >= Config::BUTTON_REBOOT_MS)
  {
    const WifiCommand cmd = WifiCommand::REBOOT;
    if (wifiCommandQueue != nullptr)
    {
      xQueueSend(wifiCommandQueue, &cmd, 0);
      rebootSent   = true;
      apCancelFlag = true;
      printf("[Button] Reboot requested after %u ms hold\n", heldMs);
    }
  }
}

void buttonTask(void* /*params*/)
{
  bool     pressed{false};
  bool     rebootSent{false};
  uint32_t pressedAt{0};

  while (true)
  {
    const bool isPressed = gpio_get(Config::BUTTON_PIN);

    if (isPressed and not pressed)
    {
      pressed   = true;
      pressedAt = nowMs();
    }
    else if (not isPressed and pressed)
    {
      handleButtonRelease(nowMs() - pressedAt, rebootSent);
      pressed = false;
    }

    if (pressed and not rebootSent)
    {
      handleButtonHold(nowMs() - pressedAt, rebootSent);
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
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
  updateErrorLedFromData(data);

  if constexpr (Config::ENABLE_HOME_ASSISTANT)
  {
    haClient.publishSensorState(now, data, irrigationController.isWatering());
  }
}

void sensorTask(void* params)
{
  auto& ctx                  = *static_cast<HomeAssistantClient*>(params);
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

    if (now - lastSensorRead >= Config::DEFAULT_SENSOR_READ_INTERVAL_MS)
    {
      handleSensorRead(now, sensorManager, irrigationController, ctx);
      lastSensorRead = now;
    }

    vTaskDelay(pdMS_TO_TICKS(sensorTaskTickMs));
  }
}

void irrigationTask(void* params)
{
  auto& irrigationController = *static_cast<IrrigationController*>(params);
  while (true)
  {
    irrigationController.update();
    const auto sleepMs = irrigationController.nextSleepHintMs();
    vTaskDelay(pdMS_TO_TICKS(sleepMs));
  }
}

struct ProvisionContext
{
  WifiProvisioner*     provisioner{};
  HomeAssistantClient* haClient{};
  QueueHandle_t        queue{};
};

void processWifiCommand(WifiCommand cmd, ProvisionContext* ctx, bool& connected, bool& apActive)
{
  switch (cmd)
  {
    case WifiCommand::START_PROVISIONING:
    {
      if (apActive)
      {
        apCancelFlag = true;
        printf("[WiFi] AP stop requested\n");
        break;
      }

      printf("[WiFi] Button requested AP provisioning\n");
      setNetworkLedState(NetworkLedState::PROVISIONING);
      apCancelFlag = false;
      apActive     = true;
      apActiveFlag = true;

      auto newCreds = ctx->provisioner->startApAndServe(Config::AP_SESSION_TIMEOUT_MS, &apCancelFlag);
      printf("[WiFi] Provisioning task returned (valid=%d)\n", newCreds.valid ? 1 : 0);

      apActive     = false;
      apActiveFlag = false;

      if (newCreds.valid)
      {
        printf("[WiFi] Provisioning done, saving credentials and rebooting...\n");
        WifiProvisioner::storeCredentials(newCreds);
        printf("[WiFi] Credentials saved, rebooting...\n");
        blinkErrorAsync(3, pdMS_TO_TICKS(200), pdMS_TO_TICKS(200));
        watchdog_reboot(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
      else
      {
        connected = connected && ctx->provisioner->isConnected();
      }

      setNetworkLedState(connected ? NetworkLedState::CONNECTED : NetworkLedState::OFF);
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

void wifiProvisionTask(void* params)
{
  auto* ctx = static_cast<ProvisionContext*>(params);
  if ((ctx == nullptr) or (ctx->provisioner == nullptr) or (ctx->haClient == nullptr)) [[unlikely]]
  {
    vTaskDelete(nullptr);
  }

  setNetworkLedState(NetworkLedState::CONNECTING);
  auto credsList = WifiProvisioner::loadStoredCredentials();
  bool connected = false;

  for (size_t i = credsList.size(); i-- > 0;)
  {
    if (credsList[i].valid)
    {
      if (ctx->provisioner->connectSta(credsList[i]))
      {
        connected = true;
        break;
      }
    }
  }
  bool apActive = false;
  apActiveFlag  = false;
  if (connected)
  {
    ctx->haClient->setWifiReady(true);
    if constexpr (Config::ENABLE_HOME_ASSISTANT)
    {
      ctx->haClient->init();
    }
    setNetworkLedState(NetworkLedState::CONNECTED);
  }
  else
  {
    ctx->haClient->setWifiReady(false);
    setNetworkLedState(NetworkLedState::OFF);
    printf("[WiFi] No valid connection.\n");
  }

  while (true)
  {
    WifiCommand cmd{};
    if ((ctx->queue != nullptr) and (xQueueReceive(ctx->queue, &cmd, pdMS_TO_TICKS(200)) == pdPASS))
    {
      processWifiCommand(cmd, ctx, connected, apActive);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

}  // namespace

void startAppTasks(SensorManager& sensorManager, IrrigationController& irrigationController,
                   HomeAssistantClient& haClient, WifiProvisioner& provisioner)
{
  haClient.setControllers(&sensorManager, &irrigationController);
  initUserInterfacePins();

  blinkErrorBlocking(3);

  ledStateMutex    = xSemaphoreCreateMutex();
  wifiCommandQueue = xQueueCreate(2, sizeof(WifiCommand));
  if ((ledStateMutex == nullptr) or (wifiCommandQueue == nullptr)) [[unlikely]]
  {
    printf("[AppTasks] Failed to create synchronization primitives\n");
  }

  static ProvisionContext ctx{};
  ctx.provisioner = &provisioner;
  ctx.haClient    = &haClient;
  ctx.queue       = wifiCommandQueue;

  xTaskCreate(wifiProvisionTask, "wifiProv", WIFI_PROV_STACK, &ctx, WIFI_PROV_PRIORITY, nullptr);

  xTaskCreate(buttonTask, "button", BUTTON_TASK_STACK, nullptr, BUTTON_TASK_PRIORITY, nullptr);
  xTaskCreate(ledTask, "leds", LED_TASK_STACK, nullptr, LED_TASK_PRIORITY, nullptr);

  xTaskCreate(sensorTask, "sensorTask", SENSOR_TASK_STACK, &haClient, SENSOR_TASK_PRIORITY, nullptr);
  xTaskCreate(irrigationTask, "irrigationTask", IRRIGATION_TASK_STACK, &irrigationController, IRRIGATION_TASK_PRIORITY,
              nullptr);

  printf("[AppTasks] Starting FreeRTOS scheduler\n");
  vTaskStartScheduler();
}
