#pragma once

#include "IrrigationController.hpp"
#include "MQTTClient.hpp"
#include "SensorManager.hpp"
#include "WifiProvisioner.hpp"

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
  CONNECTED
};

struct LedSharedState
{
  bool            errorOn{false};
  NetworkLedState network{NetworkLedState::OFF};
};

struct ProvisionContext
{
  WifiProvisioner* provisioner{};
  MQTTClient*      haClient{};
  QueueHandle_t    queue{};
};

void startAppTasks(IrrigationController& irrigationController, MQTTClient& mqttClient, WifiProvisioner& provisioner);
