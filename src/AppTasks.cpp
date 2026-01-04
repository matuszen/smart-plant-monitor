#include "AppTasks.hpp"
#include "Config.hpp"
#include "ConnectionManager.hpp"
#include "FlashManager.hpp"
#include "IrrigationController.hpp"
#include "MQTTClient.hpp"
#include "SensorManager.hpp"
#include "Types.hpp"

#include <FreeRTOS.h>
#include <hardware/gpio.h>
#include <hardware/watchdog.h>
#include <pico/platform/panic.h>
#include <pico/stdlib.h>
#include <pico/time.h>
#include <portmacrocommon.h>
#include <projdefs.h>
#include <queue.h>
#include <task.h>

#include <cstdint>
#include <cstdio>

namespace
{

extern "C"
{
  void vApplicationMallocFailedHook(void)
  {
    panic("FreeRTOS: Malloc Failed! No memory available.");
  }

  void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName)
  {
    (void)xTask;
    panic("FreeRTOS: Stack Overflow in task: %s", pcTaskName);
  }

  void vApplicationIdleHook(void)
  {
  }

  void vApplicationTickHook(void)
  {
  }
}

LedSharedState    ledShared{};
QueueHandle_t     wifiCommandQueue = nullptr;
SemaphoreHandle_t ledStateMutex    = nullptr;
volatile bool     apActiveFlag     = false;
volatile bool     apCancelFlag     = false;

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

void setSensorError(const bool on)
{
  if (ledStateMutex == nullptr) [[unlikely]]
  {
    return;
  }
  xSemaphoreTake(ledStateMutex, portMAX_DELAY);
  ledShared.sensorError = on;
  xSemaphoreGive(ledStateMutex);
}

void setWifiError(const bool on)
{
  if (ledStateMutex == nullptr) [[unlikely]]
  {
    return;
  }
  xSemaphoreTake(ledStateMutex, portMAX_DELAY);
  ledShared.wifiError = on;
  xSemaphoreGive(ledStateMutex);
}

void setActivityLedState(const bool on)
{
  if (ledStateMutex == nullptr) [[unlikely]]
  {
    return;
  }
  xSemaphoreTake(ledStateMutex, portMAX_DELAY);
  ledShared.activity = on;
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
  setSensorError(waterLow or sensorsBad);
}

void ledTask(void* const /*params*/)
{
  bool networkOn = false;

  uint32_t lastNetworkToggle = 0;

  constexpr uint32_t connectBlinkMs   = 400;
  constexpr uint32_t provisionBlinkMs = 150;
  constexpr uint32_t connectedBlinkMs = 1000;

  while (true)
  {
    const auto now = nowMs();
    auto       led = readLedState();

    // Status LED: Only on when active (watering or reading sensors)
    gpio_put(Config::LED_STATUS_PIN, led.activity);

    switch (led.network)
    {
      case NetworkLedState::MQTT_CONNECTED:
        networkOn = true;
        break;
      case NetworkLedState::CONNECTED:
        if ((now - lastNetworkToggle) >= connectedBlinkMs)
        {
          networkOn         = not networkOn;
          lastNetworkToggle = now;
        }
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

    gpio_put(Config::LED_ERROR_PIN, led.isError());

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void handleButtonRelease(const uint32_t heldMs, bool& rebootSent)
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

void handleButtonHold(const uint32_t heldMs, bool& rebootSent)
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

void buttonTask(void* const /*params*/)
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

auto handleSensorRead(const uint32_t now, SensorManager& sensorManager, IrrigationController& irrigationController,
                      MQTTClient& mqttClient) -> SensorData
{
  printf("[%u] Reading sensors...\n", now);

  const auto data = sensorManager.readAllSensors();

  logEnvironment(data);
  logLight(data);
  logSoil(data);
  logIrrigation(irrigationController);
  logWaterLevel(data);
  updateErrorLedFromData(data);

  irrigationController.update(data);

  mqttClient.publishSensorState(now, data, irrigationController.isWatering());

  return data;
}

void handleWaterLevelError(const uint32_t now, uint32_t& lastSensorRead, bool& waterLevelError,
                           SensorManager& sensorManager, IrrigationController& irrigationController, MQTTClient& ctx)
{
  const auto waterData = sensorManager.readWaterLevel();
  if (waterData.isValid() and not waterData.isLow())
  {
    waterLevelError = false;
    const auto data = handleSensorRead(now, sensorManager, irrigationController, ctx);
    waterLevelError = (data.water.isValid() and data.water.isLow());
  }
  else
  {
    SensorData dummyData{};
    dummyData.water = waterData;
    logWaterLevel(dummyData);
    updateErrorLedFromData(dummyData);
  }
  lastSensorRead = now;
}

void handleNormalSensorRead(const uint32_t now, uint32_t& lastSensorRead, bool& waterLevelError,
                            SensorManager& sensorManager, IrrigationController& irrigationController, MQTTClient& ctx)
{
  const auto data = handleSensorRead(now, sensorManager, irrigationController, ctx);
  waterLevelError = (data.water.isValid() and data.water.isLow());
  lastSensorRead  = now;
}

auto shouldPerformSensorRead(const uint32_t now, uint32_t lastSensorRead, uint32_t sensorReadInterval,
                             bool waterLevelError) -> bool
{
  if (waterLevelError)
  {
    constexpr uint32_t errorRetryIntervalMs = 15'000;
    return (now - lastSensorRead >= errorRetryIntervalMs);
  }
  return (now - lastSensorRead >= sensorReadInterval);
}

void sensorTask(void* const params)
{
  auto& ctx                  = *static_cast<MQTTClient*>(params);
  auto& sensorManager        = *ctx.getSensorManager();
  auto& irrigationController = *ctx.getIrrigationController();

  SystemConfig config;
  if (!FlashManager::loadConfig(config))
  {
    config = {};
  }

  auto sensorReadInterval = config.sensorReadIntervalMs;
  if (sensorReadInterval == 0)
  {
    sensorReadInterval = Config::DEFAULT_SENSOR_READ_INTERVAL_MS;
  }

  auto               lastSensorRead       = sensorReadInterval;
  constexpr uint32_t sensorTaskTickMs     = 100;
  constexpr uint32_t errorRetryIntervalMs = 15'000;

  bool     wasWatering             = false;
  uint32_t scheduledReadTime       = 0;
  bool     pendingPostWateringRead = false;
  bool     waterLevelError         = false;

  while (true)
  {
    const auto now = nowMs();
    ctx.loop(now);

    if (ctx.isConnected())
    {
      setNetworkLedState(NetworkLedState::MQTT_CONNECTED);
    }
    else
    {
      const auto currentLed = readLedState();
      if (currentLed.network == NetworkLedState::MQTT_CONNECTED)
      {
        setNetworkLedState(NetworkLedState::CONNECTED);
      }
    }

    const bool isWatering = irrigationController.isWatering();
    setActivityLedState(isWatering);

    if (wasWatering and not isWatering)
    {
      scheduledReadTime       = now + 60'000;
      pendingPostWateringRead = true;
    }
    wasWatering = isWatering;

    bool shouldRead     = shouldPerformSensorRead(now, lastSensorRead, sensorReadInterval, waterLevelError);
    bool onlyWaterLevel = waterLevelError and (now - lastSensorRead >= errorRetryIntervalMs);

    if (pendingPostWateringRead and now >= scheduledReadTime)
    {
      shouldRead              = true;
      onlyWaterLevel          = false;
      pendingPostWateringRead = false;
    }

    if (shouldRead)
    {
      setActivityLedState(true);

      if (onlyWaterLevel)
      {
        handleWaterLevelError(now, lastSensorRead, waterLevelError, sensorManager, irrigationController, ctx);
      }
      else
      {
        handleNormalSensorRead(now, lastSensorRead, waterLevelError, sensorManager, irrigationController, ctx);
      }

      setActivityLedState(irrigationController.isWatering());
    }

    vTaskDelay(pdMS_TO_TICKS(sensorTaskTickMs));
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

void processWifiCommand(const WifiCommand cmd, ProvisionContext* const ctx, bool& connected, bool& apActive)
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

      ctx->mqttClient->setWifiReady(false);

      const bool reboot = ctx->provisioner->startApAndServe(Config::AP::SESSION_TIMEOUT_MS,
                                                            *ctx->mqttClient->getSensorManager(), &apCancelFlag);

      apActive     = false;
      apActiveFlag = false;

      if (reboot)
      {
        printf("[WiFi] Configuration updated, rebooting...\n");
        watchdog_reboot(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
      else
      {
        SystemConfig config;
        if (FlashManager::loadConfig(config) and config.wifi.valid)
        {
          connected = ctx->provisioner->connectSta(config.wifi);
          ctx->mqttClient->setWifiReady(connected);
          if (config.mqtt.enabled)
          {
            [[maybe_unused]] const auto _ = ctx->mqttClient->init(config.mqtt);
          }
        }
        else
        {
          connected = connected and ctx->provisioner->isConnected();
          ctx->mqttClient->setWifiReady(connected);
        }
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

void handleInitialConnection(ProvisionContext* const ctx, const SystemConfig& config, bool& connected)
{
  auto connectedStatus = ctx->provisioner->connectSta(config.wifi);
  if (connectedStatus)
  {
    ctx->mqttClient->setWifiReady(true);
    if (config.mqtt.enabled)
    {
      [[maybe_unused]] const auto _ = ctx->mqttClient->init(config.mqtt);
    }
    setNetworkLedState(NetworkLedState::CONNECTED);
    setWifiError(false);
  }
  else
  {
    ctx->mqttClient->setWifiReady(false);
    setNetworkLedState(NetworkLedState::OFF);
    printf("[WiFi] No valid connection.\n");
    setWifiError(true);
  }
  connected = connectedStatus;
}

void handleConnectionRetry(ProvisionContext* const ctx, const SystemConfig& config, bool& connected,
                           uint32_t& lastConnectionAttempt)
{
  const auto         now                       = nowMs();
  constexpr uint32_t connectionRetryIntervalMs = 15'000;

  if (now - lastConnectionAttempt < connectionRetryIntervalMs)
  {
    return;
  }

  printf("[WiFi] Retrying connection...\n");
  setNetworkLedState(NetworkLedState::CONNECTING);
  connected             = ctx->provisioner->connectSta(config.wifi);
  lastConnectionAttempt = nowMs();

  if (connected)
  {
    ctx->mqttClient->setWifiReady(true);
    if (config.mqtt.enabled)
    {
      [[maybe_unused]] const auto _ = ctx->mqttClient->init(config.mqtt);
    }
    setNetworkLedState(NetworkLedState::CONNECTED);
    setWifiError(false);
  }
  else
  {
    setNetworkLedState(NetworkLedState::OFF);
    setWifiError(true);
  }
}

void wifiProvisionTask(void* const params)
{
  auto* ctx = static_cast<ProvisionContext*>(params);
  if ((ctx == nullptr) or (ctx->provisioner == nullptr) or (ctx->mqttClient == nullptr)) [[unlikely]]
  {
    vTaskDelete(nullptr);
  }

  setNetworkLedState(NetworkLedState::CONNECTING);

  SystemConfig config;
  if (not FlashManager::loadConfig(config))
  {
    config = {};
  }

  bool connected = false;
  handleInitialConnection(ctx, config, connected);

  bool apActive = false;
  apActiveFlag  = false;

  uint32_t lastConnectionAttempt = nowMs();

  while (true)
  {
    WifiCommand cmd{};
    if ((ctx->queue != nullptr) and (xQueueReceive(ctx->queue, &cmd, pdMS_TO_TICKS(100)) == pdPASS))
    {
      processWifiCommand(cmd, ctx, connected, apActive);
    }

    if (not connected and not apActive and config.wifi.valid)
    {
      handleConnectionRetry(ctx, config, connected, lastConnectionAttempt);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

}  // namespace

void startAppTasks(IrrigationController& irrigationController, MQTTClient& mqttClient, ConnectionManager& provisioner)
{
  initUserInterfacePins();

  blinkErrorBlocking(3);

  ledStateMutex    = xSemaphoreCreateMutex();
  wifiCommandQueue = xQueueCreate(2, sizeof(WifiCommand));
  if ((ledStateMutex == nullptr) or (wifiCommandQueue == nullptr)) [[unlikely]]
  {
    printf("[AppTasks] Failed to create synchronization primitives\n");
  }

  static auto ctx = ProvisionContext{
    .provisioner = &provisioner,
    .mqttClient  = &mqttClient,
    .queue       = wifiCommandQueue,
  };

  xTaskCreate(wifiProvisionTask, "wifiProv", WIFI_PROV_STACK, &ctx, WIFI_PROV_PRIORITY, nullptr);

  xTaskCreate(buttonTask, "button", BUTTON_TASK_STACK, nullptr, BUTTON_TASK_PRIORITY, nullptr);
  xTaskCreate(ledTask, "leds", LED_TASK_STACK, nullptr, LED_TASK_PRIORITY, nullptr);

  xTaskCreate(sensorTask, "sensorTask", SENSOR_TASK_STACK, &mqttClient, SENSOR_TASK_PRIORITY, nullptr);
  xTaskCreate(irrigationTask, "irrigationTask", IRRIGATION_TASK_STACK, &irrigationController, IRRIGATION_TASK_PRIORITY,
              nullptr);

  printf("[AppTasks] Starting FreeRTOS scheduler\n");
  vTaskStartScheduler();
}
