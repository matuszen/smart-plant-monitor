#include <pico/cyw43_arch.h>  // IWYU pragma: keep

#include "Config.hpp"
#include "FlashManager.hpp"
#include "SensorManager.hpp"
#include "Types.hpp"
#include "WifiProvisioner.hpp"

#include "dhcpserver.h"
#include "web/provision_page.html"

#include <FreeRTOS.h>
#include <boards/pico2_w.h>
#include <cyw43.h>
#include <cyw43_ll.h>
#include <hardware/flash.h>
#include <hardware/regs/addressmap.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include <lwip/def.h>
#include <lwip/inet.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <lwip/sockets.h>
#include <pico/time.h>
#include <projdefs.h>
#include <sys/_timeval.h>
#include <sys/select.h>
#include <task.h>

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace
{

inline constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS{30'000};

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

[[nodiscard]] auto sendAll(const int32_t client, const std::span<const char> data) -> bool
{
  size_t sent = 0;
  while (sent < data.size())
  {
    const auto remaining = data.subspan(sent);
    const auto chunkSize = std::min(remaining.size(), static_cast<size_t>(1024));
    const auto rc        = lwip_send(client, remaining.data(), static_cast<int>(chunkSize), 0);
    if (rc < 0)
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

auto configToJson(const SystemConfig& cfg) -> std::string
{
  std::array<char, 2048>      buf{};
  [[maybe_unused]] const auto _ =
    std::snprintf(buf.data(), buf.size(),
                  "{"
                  "\"wifi_ssid\":\"%s\","
                  "\"wifi_pass\":\"%s\","
                  "\"ap_ssid\":\"%s\","
                  "\"ap_pass\":\"%s\","
                  "\"mqtt_host\":\"%s\","
                  "\"mqtt_port\":%d,"
                  "\"mqtt_client_id\":\"%s\","
                  "\"mqtt_user\":\"%s\","
                  "\"mqtt_pass\":\"%s\","
                  "\"mqtt_prefix\":\"%s\","
                  "\"mqtt_topic\":\"%s\","
                  "\"mqtt_interval\":%u,"
                  "\"sensor_interval\":%u,"
                  "\"irrigation_mode\":%d"
                  "}",
                  cfg.wifi.ssid.data(), cfg.wifi.pass.data(), cfg.ap.ssid.data(), cfg.ap.pass.data(),
                  cfg.mqtt.brokerHost.data(), cfg.mqtt.brokerPort, cfg.mqtt.clientId.data(), cfg.mqtt.username.data(),
                  cfg.mqtt.password.data(), cfg.mqtt.discoveryPrefix.data(), cfg.mqtt.baseTopic.data(),
                  cfg.mqtt.publishIntervalMs, cfg.sensorReadIntervalMs, static_cast<int>(cfg.irrigationMode));
  return {buf.data()};
}

auto sensorsToJson(SensorManager& sm) -> std::string
{
  const auto data = sm.readAllSensors();

  std::array<char, 512>       buf{};
  [[maybe_unused]] const auto _ =
    std::snprintf(buf.data(), buf.size(),
                  "{"
                  "\"Temperature\":\"%.1f C\","
                  "\"Humidity\":\"%.1f %% \","
                  "\"Pressure\":\"%.1f hPa\","
                  "\"Soil Moisture\":\"%.1f %%\","
                  "\"Water Level\":\"%.1f %%\","
                  "\"Light\":\"%.1f lux\""
                  "}",
                  static_cast<double>(data.environment.temperature), static_cast<double>(data.environment.humidity),
                  static_cast<double>(data.environment.pressure), static_cast<double>(data.soil.percentage),
                  static_cast<double>(data.water.percentage), static_cast<double>(data.light.lux));
  return {buf.data()};
}

auto getJsonValue(std::string_view json, std::string_view key) -> std::string
{
  const std::string keyStr = "\"" + std::string(key) + "\"";
  auto              keyPos = json.find(keyStr);
  if (keyPos == std::string::npos)
  {
    return "";
  }
  auto colonPos = json.find(':', keyPos);
  if (colonPos == std::string::npos)
  {
    return "";
  }
  auto valueStart = json.find_first_not_of(" \t\n\r", colonPos + 1);
  if (valueStart == std::string::npos)
  {
    return "";
  }

  if (json[valueStart] == '"')
  {
    auto valueEnd = json.find('"', valueStart + 1);
    return std::string(json.substr(valueStart + 1, valueEnd - valueStart - 1));
  }

  auto valueEnd = json.find_first_of(",}", valueStart);
  return std::string(json.substr(valueStart, valueEnd - valueStart));
}

void updateConfigFromJson(SystemConfig& cfg, std::string_view json)
{
  auto copyStr = [&](auto& dest, std::string_view key)
  {
    std::string val = getJsonValue(json, key);
    if (!val.empty())
    {
      std::strncpy(dest.data(), val.c_str(), dest.size() - 1);
    }
  };

  auto copyInt = [&](auto& dest, std::string_view key)
  {
    std::string val = getJsonValue(json, key);
    if (!val.empty())
    {
      dest = static_cast<std::remove_reference_t<decltype(dest)>>(std::strtoul(val.c_str(), nullptr, 10));
    }
  };

  copyStr(cfg.wifi.ssid, "wifi_ssid");
  copyStr(cfg.wifi.pass, "wifi_pass");
  if (!cfg.wifi.ssid.empty())
  {
    cfg.wifi.valid = true;
  }

  copyStr(cfg.ap.ssid, "ap_ssid");
  copyStr(cfg.ap.pass, "ap_pass");

  copyStr(cfg.mqtt.brokerHost, "mqtt_host");
  copyInt(cfg.mqtt.brokerPort, "mqtt_port");
  copyStr(cfg.mqtt.clientId, "mqtt_client_id");
  copyStr(cfg.mqtt.username, "mqtt_user");
  copyStr(cfg.mqtt.password, "mqtt_pass");
  copyStr(cfg.mqtt.discoveryPrefix, "mqtt_prefix");
  copyStr(cfg.mqtt.baseTopic, "mqtt_topic");
  copyInt(cfg.mqtt.publishIntervalMs, "mqtt_interval");

  copyInt(cfg.sensorReadIntervalMs, "sensor_interval");

  std::string modeStr = getJsonValue(json, "irrigation_mode");
  if (!modeStr.empty())
  {
    cfg.irrigationMode = static_cast<IrrigationMode>(std::stoi(modeStr));
  }
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

auto handleClientRequest(const int32_t client, SystemConfig& config, bool& rebootRequested, SensorManager& sm) -> bool
{
  std::array<char, 2048> buf{};
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
    return false;
  }

  const auto firstLine = request.substr(0, firstLineEnd);
  const auto methodEnd = firstLine.find(' ');
  const auto pathEnd =
    (methodEnd != std::string_view::npos) ? firstLine.find(' ', methodEnd + 1) : std::string_view::npos;
  if ((methodEnd == std::string_view::npos) or (pathEnd == std::string_view::npos))
  {
    return false;
  }

  const auto method = firstLine.substr(0, methodEnd);
  const auto path   = firstLine.substr(methodEnd + 1, pathEnd - methodEnd - 1);

  printf("[WiFi] Method: %.*s, Path: %.*s\n", (int)method.length(), method.data(), (int)path.length(), path.data());

  if (method == "GET")
  {
    if (path == "/")
    {
      sendResponse(client, PROVISION_PAGE_HTML);
    }
    else if (path == "/api/config")
    {
      sendResponse(client, configToJson(config), "application/json");
    }
    else if (path == "/api/sensors")
    {
      sendResponse(client, sensorsToJson(sm), "application/json");
    }
    else
    {
      sendResponse(client, "Not Found", "text/plain");
    }
  }
  else if (method == "POST")
  {
    if (path == "/api/config")
    {
      const auto bodyPos = request.find("\r\n\r\n");
      if (bodyPos != std::string_view::npos)
      {
        const auto body = request.substr(bodyPos + 4);
        updateConfigFromJson(config, body);
        [[maybe_unused]] const auto _ = FlashManager::saveConfig(config);
        sendResponse(client, R"({"status":"ok"})", "application/json");
        rebootRequested = true;
      }
    }
  }

  return true;
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

auto WifiProvisioner::startApAndServe(const uint32_t timeoutMs, SensorManager& sensorManager,
                                      const volatile bool* const cancelFlag) -> bool
{
  if (not init())
  {
    provisioning_ = false;
    return false;
  }

  provisioning_ = true;
  connected_    = false;

  cyw43_arch_disable_sta_mode();

  SystemConfig config;
  if (!FlashManager::loadConfig(config))
  {
    config = {};
  }

  const char* apSsid = (config.ap.ssid[0] != '\0') ? config.ap.ssid.data() : Config::AP_SSID;
  const char* apPass = (config.ap.pass[0] != '\0') ? config.ap.pass.data() : Config::AP_PASS;

  printf("[WiFi] Starting AP '%s'...\n", apSsid);
  cyw43_arch_enable_ap_mode(apSsid, apPass, CYW43_AUTH_WPA2_AES_PSK);

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
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_port        = htons(80);
  addr.sin_addr.s_addr = PP_HTONL(INADDR_ANY);

  lwip_bind(server, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
  lwip_listen(server, 2);

  const auto startMs         = to_ms_since_boot(get_absolute_time());
  bool       rebootRequested = false;

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
    lwip_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    handleClientRequest(client, config, rebootRequested, sensorManager);

    if (rebootRequested)
    {
      vTaskDelay(pdMS_TO_TICKS(500));
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

  return rebootRequested;
}
