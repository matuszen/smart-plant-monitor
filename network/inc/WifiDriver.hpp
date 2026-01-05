#pragma once

#include <cstdint>

class WifiDriver final
{
public:
  enum class Interface : uint8_t
  {
    STA,
    AP
  };

  WifiDriver()  = default;
  ~WifiDriver() = default;

  WifiDriver(const WifiDriver&)                    = delete;
  auto operator=(const WifiDriver&) -> WifiDriver& = delete;
  WifiDriver(WifiDriver&&)                         = delete;
  auto operator=(WifiDriver&&) -> WifiDriver&      = delete;

  [[nodiscard]] auto init() -> bool;

  [[nodiscard]] auto connectSta(const char* ssid, const char* password, uint32_t timeoutMs) -> bool;
  static void        disconnectSta();

  [[nodiscard]] auto startAp(const char* ssid, const char* password) -> bool;
  static void        stopAp();

  void        setHostname(const char* hostname) const;
  static void logIpInfo(Interface interface);

private:
  bool initialized_{false};
};
