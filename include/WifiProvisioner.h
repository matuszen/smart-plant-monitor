#pragma once

#include <array>
#include <cstdint>

struct WifiCredentials
{
  std::array<char, 33> ssid{};
  std::array<char, 65> password{};
  bool                 valid{false};
};

class WifiProvisioner
{
public:
  WifiProvisioner()  = default;
  ~WifiProvisioner() = default;

  WifiProvisioner(const WifiProvisioner&)                    = delete;
  auto operator=(const WifiProvisioner&) -> WifiProvisioner& = delete;
  WifiProvisioner(WifiProvisioner&&)                         = delete;
  auto operator=(WifiProvisioner&&) -> WifiProvisioner&      = delete;

  [[nodiscard]] auto        init() -> bool;
  [[nodiscard]] static auto loadStoredCredentials() -> std::array<WifiCredentials, 10>;

  [[nodiscard]] auto connectSta(const WifiCredentials& creds) -> bool;
  [[nodiscard]] auto startApAndServe(uint32_t timeoutMs, const volatile bool* cancelFlag = nullptr) -> WifiCredentials;
  static void        storeCredentials(const WifiCredentials& creds);

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
