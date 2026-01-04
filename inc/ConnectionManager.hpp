#pragma once

#include "SensorManager.hpp"
#include "Types.hpp"

#include <cstdint>

class ConnectionManager
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

  [[nodiscard]] auto isConnected() const noexcept -> bool
  {
    return connected_;
  }

  [[nodiscard]] auto isProvisioning() const noexcept -> bool
  {
    return provisioning_;
  }

private:
  [[nodiscard]] static auto flashStorageOffset() -> uint32_t;

  bool initialized_{false};
  bool connected_{false};
  bool provisioning_{false};
};
