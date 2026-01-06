#pragma once

#include "SensorController.hpp"

#include "Types.hpp"
#include "WifiDriver.hpp"

#include <cstdint>

class ConnectionController final
{
public:
  ConnectionController()  = default;
  ~ConnectionController() = default;

  ConnectionController(const ConnectionController&)                    = delete;
  auto operator=(const ConnectionController&) -> ConnectionController& = delete;
  ConnectionController(ConnectionController&&)                         = delete;
  auto operator=(ConnectionController&&) -> ConnectionController&      = delete;

  auto init() -> bool;

  auto connectSta(const WifiCredentials& creds) -> bool;
  auto startApAndServe(uint32_t timeoutMs, SensorController& sensorController,
                       const volatile bool* cancelFlag = nullptr) -> bool;

  auto isConnected() const -> bool;

  auto isProvisioning() const -> bool;

private:
  static auto flashStorageOffset() -> uint32_t;

  bool initialized_  = false;
  bool connected_    = false;
  bool provisioning_ = false;

  WifiDriver wifiDriver_;
};
