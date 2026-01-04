#pragma once

#include "ConnectionManager.hpp"
#include "IrrigationController.hpp"
#include "MQTTClient.hpp"
#include "SensorManager.hpp"

#include <portmacrocommon.h>

#include <cstdint>

inline constexpr UBaseType_t SENSOR_TASK_PRIORITY     = tskIDLE_PRIORITY + 2;
inline constexpr UBaseType_t IRRIGATION_TASK_PRIORITY = tskIDLE_PRIORITY + 1;
inline constexpr UBaseType_t WIFI_PROV_PRIORITY       = tskIDLE_PRIORITY + 1;
inline constexpr UBaseType_t BUTTON_TASK_PRIORITY     = tskIDLE_PRIORITY + 4;
inline constexpr UBaseType_t LED_TASK_PRIORITY        = tskIDLE_PRIORITY + 1;

inline constexpr uint16_t SENSOR_TASK_STACK     = 2048;
inline constexpr uint16_t IRRIGATION_TASK_STACK = 1024;
inline constexpr uint16_t WIFI_PROV_STACK       = 2048;
inline constexpr uint16_t BUTTON_TASK_STACK     = 768;
inline constexpr uint16_t LED_TASK_STACK        = 768;

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
  CONNECTED,
  MQTT_CONNECTED
};

struct LedSharedState
{
  bool            sensorError{false};
  bool            wifiError{false};
  bool            activity{false};
  NetworkLedState network{NetworkLedState::OFF};

  [[nodiscard]] auto isError() const -> bool
  {
    return sensorError or wifiError;
  }
};

struct ProvisionContext
{
  ConnectionManager* provisioner{};
  MQTTClient*        mqttClient{};
  QueueHandle_t      queue{};
};

void startAppTasks(IrrigationController& irrigationController, MQTTClient& mqttClient, ConnectionManager& provisioner);
