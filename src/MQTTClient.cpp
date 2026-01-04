#include "MQTTClient.hpp"
#include "Config.hpp"
#include "IrrigationController.hpp"
#include "SensorManager.hpp"
#include "Types.hpp"

#include <lwip/apps/mqtt.h>
#include <lwip/dns.h>
#include <lwip/err.h>
#include <lwip/ip4_addr.h>
#include <lwip/ip_addr.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace
{

[[nodiscard]] constexpr auto hasValue(const char* const text) -> bool
{
  return (text != nullptr) and (std::char_traits<char>::length(text) > 0);
}

}  // namespace

MQTTClient::MQTTClient(SensorManager* const sensorManager, IrrigationController* const irrigationController)
  : sensorManager_(sensorManager), irrigationController_(irrigationController)
{
}

MQTTClient::~MQTTClient()
{
  if (mqttClient_ != nullptr)
  {
    mqtt_client_free(mqttClient_);
    mqttClient_ = nullptr;
  }
}

auto MQTTClient::isConnected() const noexcept -> bool
{
  return connectionState_ == ConnectionState::CONNECTED;
}

auto MQTTClient::init(const MqttConfig& config) -> bool
{
  config_ = config;
  if (not config_.enabled)
  {
    return true;
  }

  const auto formatTopic = [&](char* const buffer, const size_t size, const char* const suffix) -> int32_t
  { return std::snprintf(buffer, size, "%s/%s", config_.baseTopic.data(), suffix); };

  if (formatTopic(availabilityTopic_.data(), availabilityTopic_.size(), "availability") < 0)
  {
    printf("[MQTTClient] ERROR: availability topic truncated\n");
  }
  if (formatTopic(stateTopic_.data(), stateTopic_.size(), "state") < 0)
  {
    printf("[MQTTClient] ERROR: state topic truncated\n");
  }
  if (formatTopic(commandTopic_.data(), commandTopic_.size(), "command") < 0)
  {
    printf("[MQTTClient] ERROR: command topic truncated\n");
  }

  printf("[MQTTClient] Initializing MQTT integration...\n");

  if (not wifiReady_)
  {
    printf("[MQTTClient] Waiting for Wi-Fi...\n");
    return false;
  }

  mqttClient_ = mqtt_client_new();
  if (mqttClient_ == nullptr) [[unlikely]]
  {
    printf("[MQTTClient] ERROR: mqtt_client_new failed\n");
    return false;
  }

  mqtt_set_inpub_callback(mqttClient_, MQTTClient::mqttIncomingPublishCb, MQTTClient::mqttIncomingDataCb, this);

  printf("[MQTTClient] MQTT client ready\n");
  return true;
}

void MQTTClient::loop(const uint32_t nowMs)
{
  if (not config_.enabled)
  {
    return;
  }

  ensureMqtt(nowMs);
}

void MQTTClient::publishSensorState(const uint32_t nowMs, const SensorData& data, const bool watering, const bool force)
{
  if (not config_.enabled)
  {
    return;
  }

  lastData_ = data;
  hasData_  = true;

  if (connectionState_ != ConnectionState::CONNECTED) [[unlikely]]
  {
    return;
  }

  if (not force and (nowMs - lastPublish_) < config_.publishIntervalMs)
  {
    return;
  }

  std::array<char, 256> payload{};

  const auto isLightDataValid = data.light.isValid();
  const auto isWaterDataValid = data.water.isValid();
  const auto lightLux         = isLightDataValid ? data.light.lux : 0.0F;
  const auto waterPct         = isWaterDataValid ? data.water.percentage : 0.0F;

  const auto payloadLen = std::snprintf(
    payload.data(), payload.size(),
    "{\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f,"
    "\"soil_moisture\":%.2f,\"light_lux\":%.2f,\"light_available\":%s,"
    "\"water_level\":%.2f,\"water_level_available\":%s,\"watering\":%s}",
    data.environment.temperature, data.environment.humidity, data.environment.pressure, data.soil.percentage, lightLux,
    isLightDataValid ? "true" : "false", waterPct, isWaterDataValid ? "true" : "false", watering ? "true" : "false");

  if (payloadLen < 0 or static_cast<size_t>(payloadLen) >= payload.size())
  {
    printf("[MQTTClient] mqtt_publish skipped (payload truncated)\n");
    return;
  }

  const auto error = mqtt_publish(mqttClient_, stateTopic_.data(), payload.data(), static_cast<uint16_t>(payloadLen), 0,
                                  1, nullptr, nullptr);
  if (error != ERR_OK) [[unlikely]]
  {
    printf("[MQTTClient] mqtt_publish failed (%d)\n", error);
    return;
  }

  lastPublish_ = nowMs;
}

void MQTTClient::ensureMqtt(const uint32_t nowMs)
{
  if (not wifiReady_ or mqttClient_ == nullptr)
  {
    return;
  }

  if (mqtt_client_is_connected(mqttClient_) == 1)
  {
    if (connectionState_ != ConnectionState::CONNECTED)
    {
      connectionState_ = ConnectionState::CONNECTED;
    }
    return;
  }

  if (connectionState_ == ConnectionState::CONNECTED)
  {
    connectionState_    = ConnectionState::DISCONNECTED;
    discoveryPublished_ = false;
  }

  if (connectionState_ == ConnectionState::CONNECTING or connectionState_ == ConnectionState::RESOLVING_DNS)
  {
    return;
  }

  if ((nowMs - lastMqttAttempt_) < (Config::MQTT::RECONNECT_INTERVAL_MS * mqttBackoffMultiplier_))
  {
    return;
  }

  lastMqttAttempt_ = nowMs;

  if (not brokerIpValid_)
  {
    if (not resolveBrokerIp())
    {
      printf("[MQTTClient] Waiting for broker DNS resolution...\n");
      mqttBackoffMultiplier_ = std::min<uint32_t>(mqttBackoffMultiplier_ * 2, 12);
    }
    return;
  }

  connectMqtt();
}

void MQTTClient::connectMqtt()
{
  const auto info = mqtt_connect_client_info_t{
    .client_id   = config_.clientId.data(),
    .client_user = hasValue(config_.username.data()) ? config_.username.data() : nullptr,
    .client_pass = hasValue(config_.password.data()) ? config_.password.data() : nullptr,
  };

  printf("[MQTTClient] Connecting to MQTT broker %s:%u...\n", config_.brokerHost.data(),
         static_cast<unsigned>(config_.brokerPort));

  connectionState_ = ConnectionState::CONNECTING;
  const auto error =
    mqtt_client_connect(mqttClient_, &brokerIp_, config_.brokerPort, MQTTClient::mqttConnectionCb, this, &info);

  if (error != ERR_OK) [[unlikely]]
  {
    printf("[MQTTClient] mqtt_connect failed (%d)\n", error);
    connectionState_       = ConnectionState::DISCONNECTED;
    mqttBackoffMultiplier_ = std::min<uint32_t>(mqttBackoffMultiplier_ * 2, 12);
  }
}

void MQTTClient::publishDiscovery()
{
  if (discoveryPublished_)
  {
    return;
  }

  publishSensorDiscovery("sensor", "temperature", "Temperature", "{{ value_json.temperature }}", "°C", "temperature");
  publishSensorDiscovery("sensor", "humidity", "Humidity", "{{ value_json.humidity }}", "%", "humidity");
  publishSensorDiscovery("sensor", "pressure", "Air Pressure", "{{ value_json.pressure }}", "hPa", "pressure");
  publishSensorDiscovery("sensor", "soil", "Soil Moisture", "{{ value_json.soil_moisture }}", "%", "moisture");
  publishSensorDiscovery("sensor", "light", "Ambient Light", "{{ value_json.light_lux }}", "lx", "illuminance");
  publishSensorDiscovery("sensor", "water", "Water Level", "{{ value_json.water_level }}", "%");
  publishSensorDiscovery("binary_sensor", "watering", "Irrigation Running", "{{ value_json.watering }}", {}, "running");
  publishSwitchDiscovery();

  discoveryPublished_ = true;
}

void MQTTClient::publishAvailability(const bool online)
{
  if (mqttClient_ == nullptr)
  {
    return;
  }

  const std::string_view payload = online ? "online" : "offline";
  mqtt_publish(mqttClient_, availabilityTopic_.data(), payload.data(), static_cast<uint16_t>(payload.size()), 1, 1,
               nullptr, nullptr);
}

void MQTTClient::publishSensorDiscovery(std::string_view component, std::string_view objectId, std::string_view name,
                                        std::string_view valueTemplate, std::string_view unit,
                                        std::string_view deviceClass)
{
  std::array<char, 128> topic{};

  const size_t topicLen = std::snprintf(topic.data(), topic.size(), "%s/%.*s/%.*s/config",
                                        config_.discoveryPrefix.data(), static_cast<int32_t>(component.size()),
                                        component.data(), static_cast<int32_t>(objectId.size()), objectId.data());

  if (topicLen < 0 or topicLen >= topic.size())
  {
    printf("[MQTTClient] Discovery topic truncated, skipping\n");
    return;
  }

  std::array<char, 512> payload{};
  int32_t               offset = 0;

  const auto append = [&](const char* const fmt, const auto... args) -> auto
  {
    if (offset < 0)
    {
      return;
    }
    const auto   remaining = std::span(payload).subspan(offset);
    const size_t written   = std::snprintf(remaining.data(), remaining.size(), fmt, args...);
    if (written < 0 or written >= remaining.size())
    {
      offset = -1;
    }
    else
    {
      offset += written;
    }
  };

  append(R"({"name":"%.*s","uniq_id":"%s_%.*s","stat_t":"%s","val_tpl":"%.*s","avty_t":"%s")",
         static_cast<int32_t>(name.size()), name.data(), Config::System::IDENTIFIER,
         static_cast<int32_t>(objectId.size()), objectId.data(), stateTopic_.data(),
         static_cast<int32_t>(valueTemplate.size()), valueTemplate.data(), availabilityTopic_.data());

  append(R"(,"device":{"ids":["%s"],"name":"%.*s"})", Config::System::IDENTIFIER,
         static_cast<int32_t>(Config::System::NAME.size()), Config::System::NAME.data());

  if (not deviceClass.empty())
  {
    append(R"(,"dev_cla":"%.*s")", static_cast<int32_t>(deviceClass.size()), deviceClass.data());
  }
  if (not unit.empty())
  {
    append(R"(,"unit_of_meas":"%.*s")", static_cast<int32_t>(unit.size()), unit.data());
  }

  append("}");

  if (offset < 0)
  {
    printf("[MQTTClient] Discovery payload truncated\n");
    return;
  }

  mqtt_publish(mqttClient_, topic.data(), payload.data(), static_cast<uint16_t>(offset), 1, 1, nullptr, nullptr);
}

void MQTTClient::publishSwitchDiscovery()
{
  std::array<char, 128> topic{};

  const size_t topicLen = std::snprintf(topic.data(), topic.size(), "%s/switch/%s_water/config",
                                        Config::MQTT::DEFAULT_DISCOVERY_PREFIX, Config::System::IDENTIFIER);
  if (topicLen < 0 or topicLen >= topic.size())
  {
    printf("[MQTTClient] Switch discovery topic truncated, skipping\n");
    return;
  }

  std::array<char, 512> payload{};
  int32_t               offset = 0;

  const auto append = [&](const char* const fmt, const auto... args) -> auto
  {
    if (offset < 0)
    {
      return;
    }
    const auto   remaining = std::span(payload).subspan(offset);
    const size_t written   = std::snprintf(remaining.data(), remaining.size(), fmt, args...);
    if (written < 0 or written >= remaining.size())
    {
      offset = -1;
    }
    else
    {
      offset += written;
    }
  };

  append(R"({"name":"Irrigation Switch","uniq_id":"%s_water","cmd_t":"%s","stat_t":"%s","avty_t":"%s")",
         Config::System::IDENTIFIER, commandTopic_.data(), stateTopic_.data(), availabilityTopic_.data());

  append(R"(,"pl_on":"ON","pl_off":"OFF","stat_on":"ON","stat_off":"OFF")");
  append(R"(,"val_tpl":"{{ 'ON' if value_json.watering else 'OFF' }}")");

  append(R"(,"device":{"ids":["%s"],"name":"%.*s"}})", Config::System::IDENTIFIER,
         static_cast<int32_t>(Config::System::NAME.size()), Config::System::NAME.data());

  if (offset < 0)
  {
    printf("[MQTTClient] Switch discovery payload truncated\n");
    return;
  }

  mqtt_publish(mqttClient_, topic.data(), payload.data(), static_cast<uint16_t>(offset), 1, 1, nullptr, nullptr);
}

void MQTTClient::subscribeToCommands()
{
  mqtt_subscribe(mqttClient_, commandTopic_.data(), 1, nullptr, nullptr);
}

void MQTTClient::handleCommand(const std::string_view payload)
{
  if (payload.empty())
  {
    return;
  }

  printf("[MQTTClient] Command received: %.*s\n", static_cast<int32_t>(payload.size()), payload.data());

  if (payload == "ON")
  {
    irrigationController_->setMode(IrrigationMode::MANUAL);
    irrigationController_->startWatering(Config::DEFAULT_WATERING_DURATION_MS);
  }
  else if (payload == "OFF")
  {
    irrigationController_->stopWatering();
    irrigationController_->setMode(IrrigationMode::OFF);
  }
  else if (payload == "HUMIDITY")
  {
    irrigationController_->setMode(IrrigationMode::HUMIDITY);
  }

  if (hasData_)
  {
    publishSensorState(lastPublish_, lastData_, irrigationController_->isWatering(), true);
  }
}

void MQTTClient::mqttConnectionCb(mqtt_client_t* const client, void* const arg, const mqtt_connection_status_t status)
{
  (void)client;
  auto* self = static_cast<MQTTClient*>(arg);
  if (status != MQTT_CONNECT_ACCEPTED) [[unlikely]]
  {
    self->connectionState_       = ConnectionState::DISCONNECTED;
    self->discoveryPublished_    = false;
    self->mqttBackoffMultiplier_ = std::min<uint32_t>(self->mqttBackoffMultiplier_ * 2, 12);
    printf("[MQTTClient] MQTT connection failed (%d)\n", status);
    return;
  }

  printf("[MQTTClient] MQTT connected\n");
  self->connectionState_       = ConnectionState::CONNECTED;
  self->mqttBackoffMultiplier_ = 1;
  self->publishAvailability(true);
  self->publishDiscovery();
  self->subscribeToCommands();

  if (self->hasData_)
  {
    self->publishSensorState(self->lastPublish_, self->lastData_, self->irrigationController_->isWatering(), true);
  }
}

void MQTTClient::mqttIncomingPublishCb(void* const arg, const char* const topic, const uint32_t tot_len)
{
  auto* self = static_cast<MQTTClient*>(arg);
  (void)tot_len;
  std::strncpy(self->lastIncomingTopic_.data(), topic, self->lastIncomingTopic_.size() - 1);
  self->lastIncomingTopic_.back() = '\0';
}

void MQTTClient::mqttIncomingDataCb(void* const arg, const uint8_t* const data, const uint16_t len, const uint8_t flags)
{
  auto* self = static_cast<MQTTClient*>(arg);
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
    self->handleCommand(std::string_view(self->incomingBuffer_.data(), copyLen));
  }
}

auto MQTTClient::resolveBrokerIp() -> bool
{
  if (ipaddr_aton(config_.brokerHost.data(), &brokerIp_) != 0)
  {
    brokerIpValid_ = true;
    return true;
  }

  connectionState_ = ConnectionState::RESOLVING_DNS;
  const err_t err  = dns_gethostbyname(config_.brokerHost.data(), &brokerIp_, MQTTClient::dnsFoundCb, this);
  if (err == ERR_OK)
  {
    brokerIpValid_   = true;
    connectionState_ = ConnectionState::DISCONNECTED;
    return true;
  }
  if (err == ERR_INPROGRESS)
  {
    return false;
  }

  printf("[MQTTClient] DNS lookup failed (%d)\n", err);
  connectionState_ = ConnectionState::DISCONNECTED;
  return false;
}

void MQTTClient::dnsFoundCb(const char* /*name*/, const ip_addr_t* const ipaddr, void* const arg)
{
  auto* self = static_cast<MQTTClient*>(arg);
  self->handleDnsResult(ipaddr);
}

void MQTTClient::handleDnsResult(const ip_addr_t* result)
{
  connectionState_ = ConnectionState::DISCONNECTED;

  if (result == nullptr) [[unlikely]]
  {
    printf("[MQTTClient] DNS resolution failed\n");
    brokerIpValid_ = false;
    return;
  }

  brokerIp_      = *result;
  brokerIpValid_ = true;

  std::array<char, 16> ipStr{};
  ip4addr_ntoa_r(ip_2_ip4(&brokerIp_), ipStr.data(), ipStr.size());
  printf("[MQTTClient] Broker resolved to %s\n", ipStr.data());
}
