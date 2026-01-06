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

  [[nodiscard]] auto init() -> bool;

  [[nodiscard]] auto connectSta(const WifiCredentials& creds) -> bool;
  [[nodiscard]] auto startApAndServe(uint32_t timeoutMs, SensorController& sensorController,
                                     const volatile bool* cancelFlag = nullptr) -> bool;

  [[nodiscard]] auto isConnected() const -> bool
  {
    return connected_;
  }

  [[nodiscard]] auto isProvisioning() const -> bool
  {
    return provisioning_;
  }

private:
  [[nodiscard]] static auto flashStorageOffset() -> uint32_t;

  bool initialized_  = false;
  bool connected_    = false;
  bool provisioning_ = false;

  WifiDriver wifiDriver_;
};
