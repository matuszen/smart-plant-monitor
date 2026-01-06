#include "AppContext.hpp"

#include <FreeRTOS.h>
#include <portmacrocommon.h>
#include <semphr.h>

auto LedSharedState::isError() const -> bool
{
  return sensorError or wifiError;
}

void AppContext::setNetworkLedState(const NetworkLedState state)
{
  if (ledStateMutex == nullptr) [[unlikely]]
  {
    return;
  }
  xSemaphoreTake(ledStateMutex, portMAX_DELAY);
  ledState.network = state;
  xSemaphoreGive(ledStateMutex);
}

void AppContext::setSensorError(const bool on)
{
  if (ledStateMutex == nullptr) [[unlikely]]
  {
    return;
  }
  xSemaphoreTake(ledStateMutex, portMAX_DELAY);
  ledState.sensorError = on;
  xSemaphoreGive(ledStateMutex);
}

void AppContext::setWifiError(const bool on)
{
  if (ledStateMutex == nullptr) [[unlikely]]
  {
    return;
  }
  xSemaphoreTake(ledStateMutex, portMAX_DELAY);
  ledState.wifiError = on;
  xSemaphoreGive(ledStateMutex);
}

void AppContext::setActivityLedState(const bool on)
{
  if (ledStateMutex == nullptr) [[unlikely]]
  {
    return;
  }
  xSemaphoreTake(ledStateMutex, portMAX_DELAY);
  ledState.activity = on;
  xSemaphoreGive(ledStateMutex);
}

auto AppContext::readLedState() const -> LedSharedState
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
