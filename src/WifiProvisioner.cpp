#include <pico/cyw43_arch.h>  // IWYU pragma: keep

#include "Config.hpp"
#include "WifiProvisioner.hpp"

#include "dhcpserver.h"
#include "web/connecting_page.hpp"
#include "web/provision_page.hpp"
#include "web/success_page.hpp"

#include <FreeRTOS.h>
#include <cstring>
#include <projdefs.h>
#include <task.h>

#include <boards/pico2_w.h>
#include <cyw43.h>
#include <cyw43_ll.h>
#include <hardware/flash.h>
#include <hardware/regs/addressmap.h>
#include <hardware/sync.h>
#include <lwip/def.h>
#include <lwip/inet.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <lwip/sockets.h>
#include <pico/time.h>
#include <sys/_timeval.h>
#include <sys/select.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

namespace
{

inline constexpr uint32_t CRED_FLASH_SECTOR_SIZE{FLASH_SECTOR_SIZE};
inline constexpr uint32_t CRED_FLASH_MAGIC{0x57'49'46'49};
inline constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS{30'000};

struct FlashRecord
{
  uint32_t        magic{CRED_FLASH_MAGIC};
  WifiCredentials creds{};
  uint32_t        crc{0};
};

[[nodiscard]] auto crc32(const void* data, const size_t len) -> uint32_t
{
  const auto*              bytes = static_cast<const uint8_t*>(data);
  std::span<const uint8_t> s(bytes, len);
  uint32_t                 crc = 0xFFFFFFFF;
  for (const auto byte : s)
  {
    crc ^= byte;
    for (int j = 0; j < 8; ++j)
    {
      const auto mask = -(crc & 1U);
      crc             = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

void percentDecode(const std::span<char> str)
{
  size_t r = 0;
  size_t w = 0;
  while (r < str.size() and str[r] != '\0')
  {
    if (str[r] == '+')
    {
      str[w++] = ' ';
      r++;
    }
    else if (str[r] == '%' and (r + 2 < str.size()) and std::isxdigit(static_cast<unsigned char>(str[r + 1])) and
             std::isxdigit(static_cast<unsigned char>(str[r + 2])))
    {
      const std::array<char, 3> hex{
        {str[r + 1], str[r + 2], '\0'}
      };
      str[w++]  = static_cast<char>(strtol(hex.data(), nullptr, 16));
      r        += 3;
    }
    else
    {
      str[w++] = str[r++];
    }
  }
  if (w < str.size())
  {
    str[w] = '\0';
  }
}

[[nodiscard]] auto parseFormField(const std::string_view body, const std::string_view key,
                                  const std::span<char> out) -> bool
{
  const auto pos = body.find(key);
  if (pos == std::string_view::npos)
  {
    return false;
  }

  const auto valueStart = pos + key.length();
  const auto amp        = body.find('&', valueStart);
  const auto len        = (amp == std::string_view::npos) ? (body.length() - valueStart) : (amp - valueStart);
  const auto bytes      = std::min(len, out.size() - 1);

  body.copy(out.data(), bytes, valueStart);
  out[bytes] = '\0';
  percentDecode(out.subspan(0, bytes + 1));
  return true;
}

[[nodiscard]] auto sendAll(const int32_t client, const std::span<const char> data) -> bool
{
  size_t sent = 0;
  while (sent < data.size())
  {
    const auto remaining = data.subspan(sent);
    const auto rc        = lwip_send(client, remaining.data(), static_cast<int>(remaining.size()), 0);
    if (rc <= 0)
    {
      printf("[WiFi] send failed (%d) after %zu/%zu bytes\n", rc, sent, data.size());
      return false;
    }
    sent += static_cast<size_t>(rc);
  }
  return true;
}

void sendResponse(const int32_t client, const std::string_view body, const char* contentType = "text/html")
{
  printf("[WiFi] Sending response (%zu bytes)...\n", body.size());
  std::array<char, 256> header{};
  const auto            len =
    std::snprintf(header.data(), header.size(),
                  "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                  contentType, body.size());

  const bool headerOk = sendAll(client, std::span<const char>(header.data(), static_cast<size_t>(len)));
  const bool bodyOk   = body.empty() ? true : sendAll(client, std::span<const char>(body.data(), body.size()));

  if (headerOk and bodyOk)
  {
    printf("[WiFi] Response sent (full)\n");
  }
  else
  {
    printf("[WiFi] Response incomplete\n");
  }
}

auto escapeHtml(const char* text) -> std::string
{
  std::string out;
  if (text == nullptr)
  {
    return out;
  }

  const std::string_view view(text);
  for (const char c : view)
  {
    switch (c)
    {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&#39;";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

auto maskPassword(const WifiCredentials& creds) -> std::string
{
  const auto len = std::strlen(creds.pass.data());
  return std::string(len, '*');
}

auto renderCurrentBlock(const WifiCredentials& stored) -> std::string
{
  if (not stored.valid)
  {
    return std::string{"No stored credentials"};
  }

  std::string block;
  block += "SSID: ";
  block += escapeHtml(stored.ssid.data());
  block += "<br>Password: ";
  block += maskPassword(stored);
  block += "<br><button type=\"button\" onclick=\"useStored()\">Use & Connect</button>";
  return block;
}

auto buildProvisionPage(const WifiCredentials& stored) -> std::string
{
  std::string       page{PROVISION_PAGE_HTML};
  const std::string placeholder{"{{CURRENT_BLOCK}}"};
  const auto        block = renderCurrentBlock(stored);
  const auto        pos   = page.find(placeholder);
  if (pos != std::string::npos)
  {
    page.replace(pos, placeholder.size(), block);
  }
  return page;
}

void applyHostname(const char* hostname)
{
  netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], hostname);
  netif_set_hostname(&cyw43_state.netif[CYW43_ITF_AP], hostname);
  printf("[WiFi] Hostname set to '%s'\n", hostname);
}

void logNetifInfo(const char* label, const netif* nif)
{
  if (nif == nullptr)
  {
    return;
  }

  std::array<char, 16> ipBuf{};
  std::array<char, 16> gwBuf{};
  std::array<char, 16> maskBuf{};

  const char* ipStr = ip4addr_ntoa(netif_ip4_addr(nif));
  if (ipStr != nullptr)
  {
    std::strncpy(ipBuf.data(), ipStr, ipBuf.size() - 1);
  }

  const char* gwStr = ip4addr_ntoa(netif_ip4_gw(nif));
  if (gwStr != nullptr)
  {
    std::strncpy(gwBuf.data(), gwStr, gwBuf.size() - 1);
  }

  const char* maskStr = ip4addr_ntoa(netif_ip4_netmask(nif));
  if (maskStr != nullptr)
  {
    std::strncpy(maskBuf.data(), maskStr, maskBuf.size() - 1);
  }

  printf("[WiFi] %s IP=%s Gateway=%s Mask=%s\n", label, (ipBuf[0] != '\0') ? ipBuf.data() : "n/a",
         (gwBuf[0] != '\0') ? gwBuf.data() : "n/a", (maskBuf[0] != '\0') ? maskBuf.data() : "n/a");
}

auto handleClientRequest(const int32_t client, WifiCredentials& creds, const WifiCredentials& stored,
                         bool& useStoredRequested) -> bool
{
  std::array<char, 1024> buf{};
  const auto             len = lwip_recv(client, buf.data(), buf.size() - 1, 0);
  if (len <= 0)
  {
    printf("[WiFi] recv failed or closed: %d\n", (int)len);
    return false;
  }
  buf.at(static_cast<size_t>(len)) = '\0';
  printf("[WiFi] Received %d bytes\n", (int)len);

  const std::string_view request(buf.data(), static_cast<size_t>(len));
  const auto             firstLineEnd = request.find("\r\n");
  if (firstLineEnd == std::string_view::npos)
  {
    sendResponse(client, buildProvisionPage(stored));
    return false;
  }

  const auto firstLine = request.substr(0, firstLineEnd);
  const auto methodEnd = firstLine.find(' ');
  const auto pathEnd =
    (methodEnd != std::string_view::npos) ? firstLine.find(' ', methodEnd + 1) : std::string_view::npos;
  if ((methodEnd == std::string_view::npos) or (pathEnd == std::string_view::npos))
  {
    sendResponse(client, buildProvisionPage(stored));
    return false;
  }

  const auto method = firstLine.substr(0, methodEnd);
  const auto path   = firstLine.substr(methodEnd + 1, pathEnd - methodEnd - 1);

  const auto bodyPos = request.find("\r\n\r\n");
  const auto body    = (bodyPos != std::string_view::npos) ? request.substr(bodyPos + 4) : std::string_view{};

  const bool isPost = (method == "POST");

  if (isPost and (path == "/use-stored"))
  {
    if (stored.valid)
    {
      useStoredRequested = true;
      sendResponse(client, CONNECTING_PAGE_HTML);
      return true;
    }
    sendResponse(client, buildProvisionPage(stored));
    return false;
  }

  if (isPost and (path == "/"))
  {
    WifiCredentials incoming{};
    const auto      ssidParsed = parseFormField(body, "ssid=", std::span(incoming.ssid));
    const auto      passParsed = parseFormField(body, "pass=", std::span(incoming.pass));
    incoming.valid             = ssidParsed and (incoming.ssid[0] != '\0');
    if (not passParsed)
    {
      incoming.pass[0] = '\0';
    }
    if (incoming.valid)
    {
      creds = incoming;
      sendResponse(client, SUCCESS_PAGE_HTML);
      return true;
    }
    sendResponse(client, buildProvisionPage(stored));
    return false;
  }

  sendResponse(client, buildProvisionPage(stored));
  return false;
}

}  // namespace

auto WifiProvisioner::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  if (cyw43_arch_init() != 0) [[unlikely]]
  {
    printf("[WiFi] cyw43_arch_init failed\n");
    return false;
  }

  applyHostname(Config::WIFI_HOSTNAME);
  cyw43_arch_enable_sta_mode();
  initialized_ = true;
  return true;
}

auto WifiProvisioner::loadStoredCredentials() -> WifiCredentials
{
  WifiCredentials creds{};
  const auto*     flashAddr = std::bit_cast<const uint8_t*>(XIP_BASE + flashStorageOffset());

  FlashRecord record;
  std::memcpy(&record, flashAddr, sizeof(record));

  if (record.magic != CRED_FLASH_MAGIC)
  {
    return creds;
  }

  const auto computed = crc32(&record.creds, sizeof(WifiCredentials));
  if (computed != record.crc) [[unlikely]]
  {
    return creds;
  }

  creds       = record.creds;
  creds.valid = (creds.ssid[0] != '\0');
  return creds;
}

void WifiProvisioner::storeCredentials(const WifiCredentials& creds)
{
  FlashRecord record{};
  record.magic = CRED_FLASH_MAGIC;
  record.creds = creds;
  record.crc   = crc32(&record.creds, sizeof(WifiCredentials));

  static_assert(sizeof(FlashRecord) <= CRED_FLASH_SECTOR_SIZE, "FlashRecord must fit within a single flash sector");

  std::array<uint8_t, CRED_FLASH_SECTOR_SIZE> page{};
  std::ranges::fill(page, 0xFF);
  std::memcpy(page.data(), &record, sizeof(record));

  const auto addr = flashStorageOffset();
  const auto ints = save_and_disable_interrupts();
  flash_range_erase(addr, CRED_FLASH_SECTOR_SIZE);
  flash_range_program(addr, page.data(), page.size());
  restore_interrupts(ints);
}

auto WifiProvisioner::connectSta(const WifiCredentials& creds) -> bool
{
  if (not creds.valid) [[unlikely]]
  {
    connected_ = false;
    return false;
  }

  if (not init()) [[unlikely]]
  {
    connected_ = false;
    return false;
  }

  cyw43_arch_enable_sta_mode();

  provisioning_ = false;
  printf("[WiFi] Connecting to SSID '%s'...\n", creds.ssid.data());
  const auto response = cyw43_arch_wifi_connect_timeout_ms(creds.ssid.data(), creds.pass.data(),
                                                           CYW43_AUTH_WPA2_AES_PSK, WIFI_CONNECT_TIMEOUT_MS);
  if (response != 0) [[unlikely]]
  {
    printf("[WiFi] Connection failed (%d)\n", response);
    connected_ = false;
    return false;
  }
  printf("[WiFi] Connected\n");
  logNetifInfo("STA", &cyw43_state.netif[CYW43_ITF_STA]);
  connected_ = true;
  return true;
}

auto WifiProvisioner::startApAndServe(const uint32_t             timeoutMs,
                                      const volatile bool* const cancelFlag) -> WifiCredentials
{
  WifiCredentials creds{};

  const auto storedCreds = loadStoredCredentials();
  bool       useStored   = false;

  if (not init())
  {
    provisioning_ = false;
    return creds;
  }

  provisioning_ = true;
  connected_    = false;

  cyw43_arch_disable_sta_mode();

  printf("[WiFi] Starting AP '%s'...\n", Config::AP_SSID);
  cyw43_arch_enable_ap_mode(Config::AP_SSID, Config::AP_PASS, CYW43_AUTH_WPA2_AES_PSK);

  ip4_addr_t gw;
  ip4_addr_t mask;
  IP4_ADDR(&gw, 192, 168, 4, 1);
  IP4_ADDR(&mask, 255, 255, 255, 0);

  netif_set_addr(&cyw43_state.netif[CYW43_ITF_AP], &gw, &mask, &gw);

  dhcp_server_t dhcpServer;
  dhcp_server_init(&dhcpServer, &gw, &mask);
  printf("[WiFi] DHCP Server started at 192.168.4.1\n");
  logNetifInfo("AP", &cyw43_state.netif[CYW43_ITF_AP]);

  const int server = lwip_socket(AF_INET, SOCK_STREAM, 0);
  if (server < 0)
  {
    printf("[WiFi] Socket create failed\n");
    provisioning_ = false;
    return creds;
  }

  sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_port        = htons(80);
  addr.sin_addr.s_addr = PP_HTONL(INADDR_ANY);

  lwip_bind(server, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
  lwip_listen(server, 2);

  const auto startMs = to_ms_since_boot(get_absolute_time());

  while (true)
  {
    const auto nowMs = to_ms_since_boot(get_absolute_time());
    if ((timeoutMs > 0U) and (nowMs - startMs >= timeoutMs)) [[unlikely]]
    {
      printf("[WiFi] AP timeout reached, stopping provisioning\n");
      break;
    }
    if ((cancelFlag != nullptr) and (*cancelFlag))
    {
      printf("[WiFi] AP provisioning cancelled\n");
      break;
    }

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(server, &readSet);
    timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 100'000;

    const int sel = lwip_select(server + 1, &readSet, nullptr, nullptr, &tv);
    if (sel <= 0)
    {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (not FD_ISSET(server, &readSet))
    {
      continue;
    }

    const auto client = lwip_accept(server, nullptr, nullptr);
    if (client < 0)
    {
      printf("[WiFi] Accept failed: %d\n", client);
      continue;
    }
    printf("[WiFi] Client connected (sock=%d)\n", client);

    const auto timeout = timeval{
      .tv_sec  = 3,
      .tv_usec = 0,
    };
    lwip_setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (handleClientRequest(client, creds, storedCreds, useStored))
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      lwip_close(client);
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    lwip_close(client);
  }

  lwip_close(server);
  dhcp_server_deinit(&dhcpServer);
  cyw43_arch_disable_ap_mode();
  provisioning_ = false;
  // STA will be re-enabled by connectSta()
  if (useStored and storedCreds.valid)
  {
    creds = storedCreds;
  }
  return creds;
}

auto WifiProvisioner::flashStorageOffset() -> uint32_t
{
  return PICO_FLASH_SIZE_BYTES - CRED_FLASH_SECTOR_SIZE;
}
