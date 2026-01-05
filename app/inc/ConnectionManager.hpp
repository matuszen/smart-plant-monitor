#pragma once

#include "SensorManager.hpp"
#include "Types.hpp"
#include "WifiDriver.hpp"

#include <cstdint>

class ConnectionManager final
{
public:
  ConnectionManager()  = default;
  ~ConnectionManager() = default;

  ConnectionManager(const ConnectionManager&)                    = delete;
  auto operator=(const ConnectionManager&) -> ConnectionManager& = delete;
  ConnectionManager(ConnectionManager&&)                         = delete;
  auto operator=(ConnectionManager&&) -> ConnectionManager&      = delete;

  [[nodiscard]] auto init() -> bool;

  [[nodiscard]] auto connectSta(const WifiCredentials& creds) -> bool;
  [[nodiscard]] auto startApAndServe(uint32_t timeoutMs, SensorManager& sensorManager,
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

  WifiDriver wifiDriver_;
  bool       initialized_{false};
  bool       connected_{false};
  bool       provisioning_{false};
};
