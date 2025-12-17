
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

#include <cyw43_ll.h>
#include <lwip/apps/mqtt.h>
#include <lwip/arch.h>
#include <lwip/dns.h>
#include <lwip/err.h>
#include <lwip/ip4_addr.h>
#include <lwip/ip_addr.h>
#include <pico/cyw43_arch.h>

#include "Config.h"
#include "HomeAssistantClient.h"
#include "IrrigationController.h"
#include "SensorManager.h"
#include "Types.h"

#ifndef LWIP_MQTT
#define LWIP_MQTT 1
#endif

namespace
{

[[nodiscard]] constexpr auto hasValue(const char* const text) -> bool
{
  return (text != nullptr) and (std::char_traits<char>::length(text) > 0);
}

}  // namespace

HomeAssistantClient::HomeAssistantClient(SensorManager* sensorManager, IrrigationController* irrigationController)
  : sensorManager_(sensorManager), irrigationController_(irrigationController)
{
  const auto baseLen = std::strlen(Config::HA_BASE_TOPIC);
  auto       result  = std::snprintf(availabilityTopic_.data(), availabilityTopic_.size(), "%.*s/availability",
                                     static_cast<int>(baseLen), Config::HA_BASE_TOPIC);
  if (result < 0 or static_cast<size_t>(result) >= availabilityTopic_.size())
  {
    printf("[HA] ERROR: Availability topic snprintf failed or truncated\n");
  }
  result = std::snprintf(stateTopic_.data(), stateTopic_.size(), "%.*s/state", static_cast<int>(baseLen),
                         Config::HA_BASE_TOPIC);
  if (result < 0 or static_cast<size_t>(result) >= stateTopic_.size())
  {
    printf("[HA] ERROR: State topic snprintf failed or truncated\n");
  }
  result = std::snprintf(commandTopic_.data(), commandTopic_.size(), "%.*s/command", static_cast<int>(baseLen),
                         Config::HA_BASE_TOPIC);
  if (result < 0 or static_cast<size_t>(result) >= commandTopic_.size())
  {
    printf("[HA] ERROR: Command topic snprintf failed or truncated\n");
  }
}

HomeAssistantClient::~HomeAssistantClient()
{
  if (Config::ENABLE_HOME_ASSISTANT)
  {
    cyw43_arch_deinit();
  }
}

auto HomeAssistantClient::init() -> bool
{
  if (not Config::ENABLE_HOME_ASSISTANT)
  {
    return true;
  }

  printf("[HA] Initializing Wi-Fi and MQTT integration...\n");

  if (cyw43_arch_init() != 0)
  {
    printf("[HA] ERROR: cyw43_arch_init failed\n");
    return false;
  }

  cyw43_arch_enable_sta_mode();

  printf("[HA] Connecting to Wi-Fi SSID '%s'...\n", Config::WIFI_SSID);
  const int wifiResult =
    cyw43_arch_wifi_connect_timeout_ms(Config::WIFI_SSID, Config::WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30'000);
  if (wifiResult != 0)
  {
    printf("[HA] ERROR: Wi-Fi connect failed (%d)\n", wifiResult);
    return false;
  }

  wifiReady_  = true;
  mqttClient_ = mqtt_client_new();
  if (mqttClient_ == nullptr)
  {
    printf("[HA] ERROR: mqtt_client_new failed\n");
    return false;
  }

  mqtt_set_inpub_callback(mqttClient_, HomeAssistantClient::mqttIncomingPublishCb,
                          HomeAssistantClient::mqttIncomingDataCb, this);

  printf("[HA] Wi-Fi connected, MQTT client ready\n");
  return true;
}

void HomeAssistantClient::pollWiFi()
{
  if (Config::ENABLE_HOME_ASSISTANT)
  {
    cyw43_arch_poll();
  }
}

void HomeAssistantClient::loop(const uint32_t nowMs)
{
  if (not Config::ENABLE_HOME_ASSISTANT)
  {
    return;
  }

  pollWiFi();
  ensureMqtt(nowMs);
}

void HomeAssistantClient::publishSensorState(const uint32_t nowMs, const SensorData& data, const bool watering,
                                             const bool force)
{
  if (not Config::ENABLE_HOME_ASSISTANT)
  {
    return;
  }

  lastData_ = data;
  hasData_  = true;

  if (not mqttConnected_)
  {
    return;
  }

  if (not force and (nowMs - lastPublish_) < Config::HA_PUBLISH_INTERVAL_MS)
  {
    return;
  }

  std::array<char, 256> payload{};
  const bool            isLightDataValid = data.light.isValid();
  const bool            isWaterDataValid = data.water.isValid();
  const float           lightLux         = isLightDataValid ? data.light.lux : 0.0F;
  const float           waterPct         = isWaterDataValid ? data.water.percentage : 0.0F;

  const int payloadLen = std::snprintf(
    payload.data(), payload.size(),
    "{\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f,"
    "\"soil_moisture\":%.2f,\"light_lux\":%.2f,\"light_available\":%s,"
    "\"water_level\":%.2f,\"water_level_available\":%s,\"watering\":%s}",
    data.environment.temperature, data.environment.humidity, data.environment.pressure, data.soil.percentage, lightLux,
    isLightDataValid ? "true" : "false", waterPct, isWaterDataValid ? "true" : "false", watering ? "true" : "false");

  if (payloadLen < 0 or static_cast<size_t>(payloadLen) >= payload.size())
  {
    printf("[HA] mqtt_publish skipped (payload truncated)\n");
    return;
  }

  const err_t err = mqtt_publish(mqttClient_, stateTopic_.data(), payload.data(), static_cast<uint16_t>(payloadLen), 0,
                                 1, nullptr, nullptr);
  if (err != ERR_OK)
  {
    printf("[HA] mqtt_publish failed (%d)\n", err);
    return;
  }

  lastPublish_ = nowMs;
}

void HomeAssistantClient::ensureMqtt(const uint32_t nowMs)
{
  if (not wifiReady_ or mqttClient_ == nullptr)
  {
    return;
  }

  if (mqtt_client_is_connected(mqttClient_))
  {
    mqttConnected_ = true;
    return;
  }

  mqttConnected_      = false;
  discoveryPublished_ = false;

  if ((nowMs - lastMqttAttempt_) < Config::HA_RECONNECT_INTERVAL_MS)
  {
    return;
  }

  lastMqttAttempt_ = nowMs;

  if (not brokerIpValid_ and not resolveBrokerIp())
  {
    printf("[HA] Waiting for broker DNS resolution...\n");
    return;
  }

  connectMqtt();
}

void HomeAssistantClient::connectMqtt()
{
  mqtt_connect_client_info_t info{};
  info.client_id   = Config::MQTT_CLIENT_ID;
  info.client_user = hasValue(Config::MQTT_USERNAME) ? Config::MQTT_USERNAME : nullptr;
  info.client_pass = hasValue(Config::MQTT_PASSWORD) ? Config::MQTT_PASSWORD : nullptr;

  printf("[HA] Connecting to MQTT broker %s:%u...\n", Config::MQTT_BROKER_HOST,
         static_cast<unsigned>(Config::MQTT_BROKER_PORT));

  const err_t err = mqtt_client_connect(mqttClient_, &brokerIp_, Config::MQTT_BROKER_PORT,
                                        HomeAssistantClient::mqttConnectionCb, this, &info);
  if (err != ERR_OK)
  {
    printf("[HA] mqtt_connect failed (%d)\n", err);
  }
}

void HomeAssistantClient::publishDiscovery()
{
  if (discoveryPublished_)
  {
    return;
  }

  publishSensorDiscovery("sensor", "temperature", "Plant Temperature", "{{ value_json.temperature }}", "°C",
                         "temperature");
  publishSensorDiscovery("sensor", "humidity", "Plant Humidity", "{{ value_json.humidity }}", "%", "humidity");
  publishSensorDiscovery("sensor", "pressure", "Air Pressure", "{{ value_json.pressure }}", "hPa", "pressure");
  publishSensorDiscovery("sensor", "soil", "Soil Moisture", "{{ value_json.soil_moisture }}", "%", "moisture");
  publishSensorDiscovery("sensor", "light", "Ambient Light", "{{ value_json.light_lux }}", "lx", "illuminance");
  publishSensorDiscovery("sensor", "water", "Water Level", "{{ value_json.water_level }}", "%");
  publishSensorDiscovery("binary_sensor", "watering", "Irrigation Running", "{{ value_json.watering }}", nullptr,
                         "running");
  publishSwitchDiscovery();

  discoveryPublished_ = true;
}

void HomeAssistantClient::publishAvailability(const bool online)
{
  if (mqttClient_ == nullptr)
  {
    return;
  }

  const char* payload = online ? "online" : "offline";
  mqtt_publish(mqttClient_, availabilityTopic_.data(), payload, static_cast<uint16_t>(std::strlen(payload)), 1, 1,
               nullptr, nullptr);
}

void HomeAssistantClient::publishSensorDiscovery(const char* component, const char* objectId, const char* name,
                                                 const char* valueTemplate, const char* unit, const char* deviceClass)
{
  std::array<char, 128> topic{};
  const auto            prefixLen = std::strlen(Config::HA_DISCOVERY_PREFIX);
  const int topicLen = std::snprintf(topic.data(), topic.size(), "%.*s/%s/%s/config", static_cast<int>(prefixLen),
                                     Config::HA_DISCOVERY_PREFIX, component, objectId);
  if (topicLen < 0 or static_cast<size_t>(topicLen) >= topic.size())
  {
    printf("[HA] Discovery topic truncated, skipping\n");
    return;
  }

  std::ostringstream payload;
  payload << R"({"name":")";
  payload << name;
  payload << R"(","uniq_id":")";
  payload << std::string(Config::DEVICE_IDENTIFIER);
  payload << "_";
  payload << objectId;
  payload << R"(","stat_t":")";
  payload << stateTopic_.data();
  payload << R"(","val_tpl":")";
  payload << valueTemplate;
  payload << R"(","avty_t":")";
  payload << availabilityTopic_.data();
  payload << R"(","device":{"ids":[")";
  payload << std::string(Config::DEVICE_IDENTIFIER);
  payload << R"("],"name":")";
  payload << Config::SYSTEM_NAME.data();
  payload << R"("})";

  if (deviceClass != nullptr)
  {
    payload << R"(,"dev_cla":")";
    payload << deviceClass;
    payload << R"(")";
  }

  if (unit != nullptr)
  {
    payload << R"(,"unit_of_meas":")";
    payload << unit;
    payload << R"(")";
  }

  payload << "}";
  const auto payloadStr = payload.str();

  mqtt_publish(mqttClient_, topic.data(), payloadStr.c_str(), static_cast<uint16_t>(payloadStr.size()), 1, 1, nullptr,
               nullptr);
}

void HomeAssistantClient::publishSwitchDiscovery()
{
  std::array<char, 128> topic{};
  const int             topicLen = std::snprintf(topic.data(), topic.size(), "%s/switch/%s_water/config",
                                                 Config::HA_DISCOVERY_PREFIX, Config::DEVICE_IDENTIFIER);
  if (topicLen < 0 or static_cast<size_t>(topicLen) >= topic.size())
  {
    printf("[HA] Switch discovery topic truncated, skipping\n");
    return;
  }

  std::string payload;
  payload.reserve(320);
  payload += R"({"name":"Irrigation Switch","uniq_id":")";
  payload += std::string(Config::DEVICE_IDENTIFIER);
  payload += R"(_water","cmd_t":")";
  payload += commandTopic_.data();
  payload += R"(","stat_t":")";
  payload += stateTopic_.data();
  payload += R"(","avty_t":")";
  payload += availabilityTopic_.data();
  payload += R"(","pl_on":"ON","pl_off":"OFF",)";
  payload += R"("stat_on":"ON","stat_off":"OFF",)";
  payload += R"("val_tpl":"{{ 'ON' if value_json.watering else 'OFF' }}","device":{"ids":[")";
  payload += std::string(Config::DEVICE_IDENTIFIER);
  payload += R"("],"name":")";
  payload.append(Config::SYSTEM_NAME.data(), Config::SYSTEM_NAME.size());
  payload += R"("}})";

  mqtt_publish(mqttClient_, topic.data(), payload.c_str(), static_cast<uint16_t>(payload.size()), 1, 1, nullptr,
               nullptr);
}

void HomeAssistantClient::subscribeToCommands()
{
  mqtt_subscribe(mqttClient_, commandTopic_.data(), 1, nullptr, nullptr);
}

void HomeAssistantClient::handleCommand(const char* payload)
{
  if (payload == nullptr)
  {
    return;
  }

  printf("[HA] Command received: %s\n", payload);

  if (std::strcmp(payload, "ON") == 0)
  {
    irrigationController_->setMode(IrrigationMode::MANUAL);
    irrigationController_->startWatering(Config::DEFAULT_WATERING_DURATION_MS);
  }
  else if (std::strcmp(payload, "OFF") == 0)
  {
    irrigationController_->stopWatering();
    irrigationController_->setMode(IrrigationMode::OFF);
  }
  else if (std::strcmp(payload, "HUMIDITY") == 0)
  {
    irrigationController_->setMode(IrrigationMode::HUMIDITY);
  }

  if (hasData_)
  {
    publishSensorState(lastPublish_, lastData_, irrigationController_->isWatering(), true);
  }
}

void HomeAssistantClient::mqttConnectionCb(mqtt_client_t* client, void* arg, mqtt_connection_status_t status)
{
  (void)client;
  auto* self = static_cast<HomeAssistantClient*>(arg);
  if (status != MQTT_CONNECT_ACCEPTED)
  {
    self->mqttConnected_      = false;
    self->discoveryPublished_ = false;
    printf("[HA] MQTT connection failed (%d)\n", status);
    return;
  }

  printf("[HA] MQTT connected\n");
  self->mqttConnected_ = true;
  self->publishAvailability(true);
  self->publishDiscovery();
  self->subscribeToCommands();

  if (self->hasData_)
  {
    self->publishSensorState(self->lastPublish_, self->lastData_, self->irrigationController_->isWatering(), true);
  }
}

void HomeAssistantClient::mqttIncomingPublishCb(void* arg, const char* topic, const uint32_t tot_len)
{
  auto* self = static_cast<HomeAssistantClient*>(arg);
  (void)tot_len;
  std::strncpy(self->lastIncomingTopic_.data(), topic, self->lastIncomingTopic_.size() - 1);
  self->lastIncomingTopic_.back() = '\0';
}

void HomeAssistantClient::mqttIncomingDataCb(void* arg, const uint8_t* data, const uint16_t len, const uint8_t flags)
{
  auto* self = static_cast<HomeAssistantClient*>(arg);
  if ((flags & MQTT_DATA_FLAG_LAST) == 0)
  {
    return;
  }

  const size_t copyLen =
    (len >= self->incomingBuffer_.size()) ? self->incomingBuffer_.size() - 1 : static_cast<size_t>(len);
  std::memcpy(self->incomingBuffer_.data(), data, copyLen);
  self->incomingBuffer_.at(copyLen) = '\0';

  if (std::strcmp(self->lastIncomingTopic_.data(), self->commandTopic_.data()) == 0)
  {
    self->handleCommand(self->incomingBuffer_.data());
  }
}

auto HomeAssistantClient::resolveBrokerIp() -> bool
{
  if (ip4addr_aton(Config::MQTT_BROKER_HOST, &brokerIp_) != 0)
  {
    brokerIpValid_ = true;
    return true;
  }

  const err_t err = dns_gethostbyname(Config::MQTT_BROKER_HOST, &brokerIp_, HomeAssistantClient::dnsFoundCb, this);
  if (err == ERR_OK)
  {
    brokerIpValid_ = true;
    return true;
  }

  if (err == ERR_INPROGRESS)
  {
    return false;
  }

  printf("[HA] DNS lookup failed (%d)\n", err);
  return false;
}

void HomeAssistantClient::dnsFoundCb(const char* /*name*/, const ip_addr_t* ipaddr, void* arg)
{
  auto* self = static_cast<HomeAssistantClient*>(arg);
  self->handleDnsResult(ipaddr);
}

void HomeAssistantClient::handleDnsResult(const ip_addr_t* result)
{
  if (result == nullptr)
  {
    printf("[HA] DNS resolution returned null\n");
    brokerIpValid_ = false;
    return;
  }

  brokerIp_      = *result;
  brokerIpValid_ = true;
  std::array<char, 16> ipStr{};
  ip4addr_ntoa_r(ip_2_ip4(&brokerIp_), ipStr.data(), ipStr.size());
  printf("[HA] Broker resolved to %s\n", ipStr.data());
}
