#include "WifiDriver.hpp"
#include "dhcpserver.h"

#include <cyw43.h>
#include <cyw43_ll.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <pico/cyw43_arch.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{

dhcp_server_t dhcpServer;

}  // namespace

auto WifiDriver::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  if (cyw43_arch_init() != 0)
  {
    printf("[WifiDriver] cyw43_arch_init failed\n");
    return false;
  }

  initialized_ = true;
  return true;
}

auto WifiDriver::connectSta(const char* const ssid, const char* const password, const uint32_t timeoutMs) -> bool
{
  if (not init())
  {
    return false;
  }

  cyw43_arch_enable_sta_mode();

  printf("[WifiDriver] Connecting to SSID '%s'...\n", ssid);
  const auto response = cyw43_arch_wifi_connect_timeout_ms(ssid, password, CYW43_AUTH_WPA2_AES_PSK, timeoutMs);
  if (response != 0)
  {
    printf("[WifiDriver] Connection failed (%d)\n", response);
    return false;
  }

  printf("[WifiDriver] Connected to STA\n");
  logIpInfo(Interface::STA);
  return true;
}

void WifiDriver::disconnectSta()
{
  cyw43_arch_disable_sta_mode();
}

auto WifiDriver::startAp(const char* const ssid, const char* const password) -> bool
{
  if (not init())
  {
    return false;
  }

  cyw43_arch_disable_sta_mode();
  printf("[WifiDriver] Starting AP '%s'...\n", ssid);
  cyw43_arch_enable_ap_mode(ssid, password, CYW43_AUTH_WPA2_AES_PSK);

  ip4_addr_t gw;
  ip4_addr_t mask;
  IP4_ADDR(&gw, 192, 168, 4, 1);
  IP4_ADDR(&mask, 255, 255, 255, 0);

  netif_set_addr(&cyw43_state.netif[CYW43_ITF_AP], &gw, &mask, &gw);

  dhcp_server_init(&dhcpServer, &gw, &mask);
  printf("[WifiDriver] DHCP Server started at 192.168.4.1\n");

  logIpInfo(Interface::AP);
  return true;
}

void WifiDriver::stopAp()
{
  dhcp_server_deinit(&dhcpServer);
  cyw43_arch_disable_ap_mode();
}

void WifiDriver::setHostname(const char* const hostname) const
{
  if (not initialized_)
  {
    return;
  }
  netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], hostname);
  netif_set_hostname(&cyw43_state.netif[CYW43_ITF_AP], hostname);
  printf("[WifiDriver] Hostname set to '%s'\n", hostname);
}

void WifiDriver::logIpInfo(const Interface interface)
{
  const auto* nif =
    (interface == Interface::STA) ? &cyw43_state.netif[CYW43_ITF_STA] : &cyw43_state.netif[CYW43_ITF_AP];
  if (nif == nullptr)
  {
    return;
  }

  std::array<char, 16> ipBuf{};
  std::array<char, 16> gwBuf{};
  std::array<char, 16> maskBuf{};

  const auto* ipStr = ip4addr_ntoa(netif_ip4_addr(nif));
  if (ipStr != nullptr)
  {
    std::strncpy(ipBuf.data(), ipStr, ipBuf.size() - 1);
  }
  const auto* gwStr = ip4addr_ntoa(netif_ip4_gw(nif));
  if (gwStr != nullptr)
  {
    std::strncpy(gwBuf.data(), gwStr, gwBuf.size() - 1);
  }
  const auto* maskStr = ip4addr_ntoa(netif_ip4_netmask(nif));
  if (maskStr != nullptr)
  {
    std::strncpy(maskBuf.data(), maskStr, maskBuf.size() - 1);
  }

  const char* label = (interface == Interface::STA) ? "STA" : "AP";
  printf("[WifiDriver] %s IP=%s Gateway=%s Mask=%s\n", label, (ipBuf[0] != '\0') ? ipBuf.data() : "n/a",
         (gwBuf[0] != '\0') ? gwBuf.data() : "n/a", (maskBuf[0] != '\0') ? maskBuf.data() : "n/a");
}
