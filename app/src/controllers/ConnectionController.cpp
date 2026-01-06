#include "ConnectionController.hpp"
#include "SensorController.hpp"

#include "Common.hpp"
#include "Config.hpp"
#include "FlashManager.hpp"
#include "Types.hpp"
#include "WifiDriver.hpp"
#include "web/provision_page.html"

#include <FreeRTOS.h>
#include <boards/pico2_w.h>
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

#include <algorithm>
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
      ++r;
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

auto sendAll(const int32_t client, const std::span<const char> data) -> bool
{
  size_t sent = 0;
  while (sent < data.size())
  {
    const auto remaining = data.subspan(sent);
    const auto chunkSize = std::min(remaining.size(), static_cast<size_t>(1'024));
    const auto rc        = lwip_send(client, remaining.data(), static_cast<int32_t>(chunkSize), 0);
    if (rc < 0)
    {
      printf("[WiFi] send failed (%d) after %zu/%zu bytes\n", rc, sent, data.size());
      return false;
    }
    sent += static_cast<size_t>(rc);
  }
  return true;
}

void sendResponse(const int32_t client, const std::string_view body, const char* const contentType = "text/html")
{
  printf("[WiFi] Sending response (%zu bytes)...\n", body.size());
  std::array<char, 256> header{};
  const auto            len =
    std::snprintf(header.data(), header.size(),
                  "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                  contentType, body.size());

  const auto headerOk = sendAll(client, std::span<const char>(header.data(), static_cast<size_t>(len)));
  const auto bodyOk   = body.empty() ? true : sendAll(client, std::span<const char>(body.data(), body.size()));

  if (headerOk and bodyOk)
  {
    printf("[WiFi] Response sent\n");
  }
  else
  {
    printf("[WiFi] Response incomplete\n");
  }
}

auto configToJson(const SystemConfig& cfg) -> std::string
{
  std::array<char, 2048> buf{};
  (void)std::snprintf(buf.data(), buf.size(),
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
                      cfg.mqtt.brokerHost.data(), cfg.mqtt.brokerPort, cfg.mqtt.clientId.data(),
                      cfg.mqtt.username.data(), cfg.mqtt.password.data(), cfg.mqtt.discoveryPrefix.data(),
                      cfg.mqtt.baseTopic.data(), cfg.mqtt.publishIntervalMs / 1000, cfg.sensorReadIntervalMs / 1000,
                      static_cast<int>(cfg.irrigationMode));
  return {buf.data()};
}

auto sensorsToJson(SensorController& sm) -> std::string
{
  const auto data = sm.readAllSensors();

  std::array<char, 512> buf{};
  (void)std::snprintf(buf.data(), buf.size(),
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

auto getJsonValue(const std::string_view json, const std::string_view key) -> std::string
{
  const auto keyStr = "\"" + std::string(key) + "\"";
  const auto keyPos = json.find(keyStr);
  if (keyPos == std::string::npos)
  {
    return "";
  }
  const auto colonPos = json.find(':', keyPos);
  if (colonPos == std::string::npos)
  {
    return "";
  }
  const auto valueStart = json.find_first_not_of(" \t\n\r", colonPos + 1);
  if (valueStart == std::string::npos)
  {
    return "";
  }

  if (json[valueStart] == '"')
  {
    const auto valueEnd = json.find('"', valueStart + 1);
    return std::string(json.substr(valueStart + 1, valueEnd - valueStart - 1));
  }

  const auto valueEnd = json.find_first_of(",}", valueStart);
  return std::string(json.substr(valueStart, valueEnd - valueStart));
}

void updateConfigFromJson(SystemConfig& cfg, const std::string_view json)
{
  const auto copyStr = [&](auto& dest, const std::string_view key) -> auto
  {
    const auto val = getJsonValue(json, key);
    if (not val.empty())
    {
      std::strncpy(dest.data(), val.c_str(), dest.size() - 1);
    }
  };

  const auto copyInt = [&](auto& dest, const std::string_view key) -> auto
  {
    const auto val = getJsonValue(json, key);
    if (not val.empty())
    {
      dest = static_cast<std::remove_reference_t<decltype(dest)>>(std::strtoul(val.c_str(), nullptr, 10));
    }
  };

  copyStr(cfg.wifi.ssid, "wifi_ssid");
  copyStr(cfg.wifi.pass, "wifi_pass");
  cfg.wifi.valid = cfg.wifi.ssid[0] != '\0';

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
  cfg.mqtt.publishIntervalMs *= 1000;

  copyInt(cfg.sensorReadIntervalMs, "sensor_interval");
  cfg.sensorReadIntervalMs *= 1000;

  const auto modeStr = getJsonValue(json, "irrigation_mode");
  if (not modeStr.empty())
  {
    cfg.irrigationMode = static_cast<IrrigationMode>(std::stoi(modeStr));
  }
}

auto handleClientRequest(const int32_t client, SystemConfig& config, bool& rebootRequested,
                         SensorController& sm) -> bool
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

  const auto request      = std::string_view(buf.data(), static_cast<size_t>(len));
  const auto firstLineEnd = request.find("\r\n");
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
        FlashManager::saveConfig(config);
        sendResponse(client, R"({"status":"ok"})", "application/json");
        rebootRequested = true;
      }
    }
  }

  return true;
}

auto runProvisioningLoop(int server, SystemConfig& config, uint32_t timeoutMs, const volatile bool* cancelFlag,
                         SensorController& sensorController) -> bool
{
  const auto startMs         = Utils::getTimeSinceBoot();
  bool       rebootRequested = false;

  while (true)
  {
    const auto now = Utils::getTimeSinceBoot();
    if ((timeoutMs > 0U) and (now - startMs >= timeoutMs)) [[unlikely]]
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

    const auto sel = lwip_select(server + 1, &readSet, nullptr, nullptr, &tv);
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

    handleClientRequest(client, config, rebootRequested, sensorController);

    if (rebootRequested)
    {
      vTaskDelay(pdMS_TO_TICKS(500));
      lwip_close(client);
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    lwip_close(client);
  }
  return rebootRequested;
}

}  // namespace

auto ConnectionController::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  if (not wifiDriver_.init()) [[unlikely]]
  {
    return false;
  }

  wifiDriver_.setHostname(Config::WiFi::HOSTNAME);
  initialized_ = true;
  return true;
}

auto ConnectionController::connectSta(const WifiCredentials& creds) -> bool
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

  provisioning_ = false;
  if (not wifiDriver_.connectSta(creds.ssid.data(), creds.pass.data(), WIFI_CONNECT_TIMEOUT_MS))
  {
    connected_ = false;
    return false;
  }

  connected_ = true;
  return true;
}

auto ConnectionController::startApAndServe(const uint32_t timeoutMs, SensorController& sensorController,
                                           const volatile bool* const cancelFlag) -> bool
{
  if (not init())
  {
    provisioning_ = false;
    return false;
  }

  provisioning_ = true;
  connected_    = false;

  auto config = SystemConfig{};
  if (not FlashManager::loadConfig(config))
  {
    config                      = {};
    config.sensorReadIntervalMs = Config::DEFAULT_SENSOR_READ_INTERVAL_MS;
    config.irrigationMode       = Config::DEFAULT_IRRIGATION_MODE;
    std::strncpy(config.ap.ssid.data(), Config::AP::DEFAULT_SSID, config.ap.ssid.size() - 1);
    std::strncpy(config.ap.pass.data(), Config::AP::DEFAULT_PASS, config.ap.pass.size() - 1);
    std::strncpy(config.wifi.ssid.data(), Config::WiFi::DEFAULT_SSID, config.wifi.ssid.size() - 1);
    std::strncpy(config.wifi.pass.data(), Config::WiFi::DEFAULT_PASS, config.wifi.pass.size() - 1);
    config.mqtt.brokerPort        = Config::MQTT::DEFAULT_BROKER_PORT;
    config.mqtt.publishIntervalMs = Config::MQTT::DEFAULT_PUBLISH_INTERVAL_MS;
    std::strncpy(config.mqtt.brokerHost.data(), Config::MQTT::DEFAULT_BROKER_HOST, config.mqtt.brokerHost.size() - 1);
    std::strncpy(config.mqtt.clientId.data(), Config::MQTT::DEFAULT_CLIENT_ID, config.mqtt.clientId.size() - 1);
    std::strncpy(config.mqtt.username.data(), Config::MQTT::DEFAULT_USERNAME, config.mqtt.username.size() - 1);
    std::strncpy(config.mqtt.password.data(), Config::MQTT::DEFAULT_PASSWORD, config.mqtt.password.size() - 1);
    std::strncpy(config.mqtt.discoveryPrefix.data(), Config::MQTT::DEFAULT_DISCOVERY_PREFIX,
                 config.mqtt.discoveryPrefix.size() - 1);
    std::strncpy(config.mqtt.baseTopic.data(), Config::MQTT::DEFAULT_BASE_TOPIC, config.mqtt.baseTopic.size() - 1);
  }

  const auto* apSsid = (config.ap.ssid[0] != '\0') ? config.ap.ssid.data() : Config::AP::DEFAULT_SSID;
  const auto* apPass = (config.ap.pass[0] != '\0') ? config.ap.pass.data() : Config::AP::DEFAULT_PASS;

  if (not wifiDriver_.startAp(apSsid, apPass))
  {
    provisioning_ = false;
    return false;
  }

  const auto server = lwip_socket(AF_INET, SOCK_STREAM, 0);
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

  const auto rebootRequested = runProvisioningLoop(server, config, timeoutMs, cancelFlag, sensorController);

  lwip_close(server);
  WifiDriver::stopAp();
  provisioning_ = false;

  return rebootRequested;
}

auto ConnectionController::isConnected() const -> bool
{
  return connected_;
}

auto ConnectionController::isProvisioning() const -> bool
{
  return provisioning_;
}
