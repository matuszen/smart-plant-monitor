#pragma once

#include "Types.hpp"

#include <FreeRTOS.h>
#include <portmacrocommon.h>
#include <queue.h>
#include <semphr.h>

#include <array>
#include <cstdint>

enum class WifiCommand : uint8_t
{
  START_PROVISIONING,
  REBOOT,
};

enum class NetworkLedState : uint8_t
{
  OFF,
  CONNECTING,
  PROVISIONING,
  CONNECTED,
  MQTT_CONNECTED,
};

struct LedSharedState
{
  bool            sensorError = false;
  bool            wifiError   = false;
  bool            activity    = false;
  NetworkLedState network     = NetworkLedState::OFF;

  auto isError() const -> bool;
};

struct AppMessage
{
  enum class Type : uint8_t
  {
    SENSOR_DATA,
    ACTIVITY_LOG
  } type = Type::SENSOR_DATA;

  SensorData sensorData;
  bool       isWatering  = false;
  bool       forceUpdate = false;

  std::array<char, 64> activityText{};
};

struct AppContext
{
  QueueHandle_t     wifiCommandQueue = nullptr;
  QueueHandle_t     sensorDataQueue  = nullptr;
  SemaphoreHandle_t ledStateMutex    = nullptr;

  LedSharedState ledState;

  volatile bool apActive = false;
  volatile bool apCancel = false;

  void setNetworkLedState(NetworkLedState state);
  void setSensorError(bool on);
  void setWifiError(bool on);
  void setActivityLedState(bool on);
  auto readLedState() const -> LedSharedState;
};
