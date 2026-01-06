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
  bool            sensorError = false;
  bool            wifiError   = false;
  bool            activity    = false;
  NetworkLedState network     = NetworkLedState::OFF;

  [[nodiscard]] auto isError() const -> bool
  {
    return sensorError or wifiError;
  }
};

struct AppMessage
{
  enum class Type : uint8_t
  {
    SENSOR_DATA,
    ACTIVITY_LOG
  } type{};

  SensorData sensorData;
  bool       isWatering{};
  bool       forceUpdate{};

  std::array<char, 64> activityText{};
};

struct AppContext
{
  QueueHandle_t     wifiCommandQueue = nullptr;
  QueueHandle_t     sensorDataQueue  = nullptr;
  SemaphoreHandle_t ledStateMutex    = nullptr;

  LedSharedState ledState{};

  volatile bool apActive = false;
  volatile bool apCancel = false;

  void setNetworkLedState(const NetworkLedState state)
  {
    if (ledStateMutex == nullptr) [[unlikely]]
    {
      return;
    }
    xSemaphoreTake(ledStateMutex, portMAX_DELAY);
    ledState.network = state;
    xSemaphoreGive(ledStateMutex);
  }

  void setSensorError(const bool on)
  {
    if (ledStateMutex == nullptr) [[unlikely]]
    {
      return;
    }
    xSemaphoreTake(ledStateMutex, portMAX_DELAY);
    ledState.sensorError = on;
    xSemaphoreGive(ledStateMutex);
  }

  void setWifiError(const bool on)
  {
    if (ledStateMutex == nullptr) [[unlikely]]
    {
      return;
    }
    xSemaphoreTake(ledStateMutex, portMAX_DELAY);
    ledState.wifiError = on;
    xSemaphoreGive(ledStateMutex);
  }

  void setActivityLedState(const bool on)
  {
    if (ledStateMutex == nullptr) [[unlikely]]
    {
      return;
    }
    xSemaphoreTake(ledStateMutex, portMAX_DELAY);
    ledState.activity = on;
    xSemaphoreGive(ledStateMutex);
  }

  [[nodiscard]] auto readLedState() const -> LedSharedState
  {
    if (ledStateMutex == nullptr) [[unlikely]]
    {
      return LedSharedState{};
    }

    xSemaphoreTake(ledStateMutex, portMAX_DELAY);
    const auto snapshot = ledState;
    xSemaphoreGive(ledStateMutex);
    return snapshot;
  }
};
