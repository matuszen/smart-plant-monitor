#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>

#include <FreeRTOS.h>
#include <projdefs.h>
#include <task.h>

#include <boards/pico2_w.h>
#include <cyw43.h>
#include <cyw43_ll.h>
#include <hardware/flash.h>
#include <hardware/regs/addressmap.h>
#include <hardware/sync.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <pico/cyw43_arch.h>
#include <pico/time.h>

#include <lwip/def.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <sys/_timeval.h>
#include <sys/select.h>

#include "../libs/inc/dhcpserver.h"
#include "Config.h"
#include "WifiProvisioner.h"

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

void sendResponse(const int32_t client, const char* body)
{
  std::array<char, 256> header{};
  const auto            len =
    std::snprintf(header.data(), header.size(),
                  "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                  std::strlen(body));
  lwip_send(client, header.data(), len, 0);
  lwip_send(client, body, static_cast<int>(std::strlen(body)), 0);
}

constexpr const auto* FORM_PAGE = "<html><body><h3>Smart Plant Monitor Wi-Fi Setup</h3>"
                                  "<form method='POST' action='/'>"
                                  "SSID:<br><input name='ssid' maxlength='32'><br>"
                                  "Password:<br><input name='pass' type='password' maxlength='64'><br><br>"
                                  "<input type='submit' value='Save & Connect'>"
                                  "</form></body></html>";

constexpr const auto* SUCCESS_RESPONSE_PAGE =
  "<html><body><h3>Credentials saved. Device will connect and reboot.</h3></body></html>";

auto handleClientRequest(const int32_t client, WifiCredentials& creds) -> bool
{
  std::array<char, 512> buf{};
  const auto            len = lwip_recv(client, buf.data(), buf.size() - 1, 0);
  if (len <= 0)
  {
    return false;
  }
  buf.at(static_cast<size_t>(len)) = '\0';

  if (std::strncmp(buf.data(), "POST", 4) == 0)
  {
    const auto* bodyPtr = std::strstr(buf.data(), "\r\n\r\n");
    if (bodyPtr != nullptr)
    {
      const auto body = std::string_view(bodyPtr).substr(4);

      const auto ssidParsed = parseFormField(body, "ssid=", std::span(creds.ssid));
      const auto passParsed = parseFormField(body, "pass=", std::span(creds.pass));
      creds.valid           = ssidParsed and (creds.ssid[0] != '\0');
      if (not passParsed)
      {
        creds.pass[0] = '\0';
      }
      if (creds.valid)
      {
        sendResponse(client, SUCCESS_RESPONSE_PAGE);
        return true;
      }
    }
    sendResponse(client, FORM_PAGE);
  }
  else
  {
    sendResponse(client, FORM_PAGE);
  }
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
  connected_ = true;
  return true;
}

auto WifiProvisioner::startApAndServe(const uint32_t             timeoutMs,
                                      const volatile bool* const cancelFlag) -> WifiCredentials
{
  WifiCredentials creds{};

  if (not init())
  {
    provisioning_ = false;
    return creds;
  }

  provisioning_ = true;
  connected_    = false;

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
    if (!FD_ISSET(server, &readSet))
    {
      continue;
    }

    const int client = lwip_accept(server, nullptr, nullptr);
    if (client < 0)
    {
      continue;
    }

    if (handleClientRequest(client, creds))
    {
      lwip_close(client);
      break;
    }
    lwip_close(client);
  }

  lwip_close(server);
  dhcp_server_deinit(&dhcpServer);
  cyw43_arch_disable_ap_mode();
  provisioning_ = false;
  return creds;
}

auto WifiProvisioner::flashStorageOffset() -> uint32_t
{
  return PICO_FLASH_SIZE_BYTES - CRED_FLASH_SECTOR_SIZE;
}
