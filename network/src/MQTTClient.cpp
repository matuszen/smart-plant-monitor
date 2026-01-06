#include "MQTTClient.hpp"

#include "Config.hpp"
#include "IrrigationController.hpp"
#include "SensorController.hpp"
#include "Types.hpp"

#include <pico/time.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace
{

constexpr auto hasValue(const char* const text) -> bool
{
  return (text != nullptr) and (std::char_traits<char>::length(text) > 0);
}

template <typename... Args>
void formatTopic(std::span<char> buffer, const char* fmt, Args... args)
{
  (void)std::snprintf(buffer.data(), buffer.size(), fmt, args...);
}

template <size_t N>
void buildDiscoveryJson(std::array<char, N>& payload, const char* name, const char* uniqueId, const char* cmdTopic,
                        const char* stateTopic, const char* availabilityTopic, const char* deviceClass = nullptr,
                        const char* unit = nullptr, const char* valueTemplate = nullptr, const char* options = nullptr,
                        const char* min = nullptr, const char* max = nullptr)
{
  int        offset = 0;
  const auto append = [&](const char* fmt, auto... args) -> void
  {
    if (offset < 0)
    {
      return;
    }
    const auto remaining = std::span(payload).subspan(offset);
    const auto written   = std::snprintf(remaining.data(), remaining.size(), fmt, args...);
    if (written < 0 or static_cast<size_t>(written) >= remaining.size())
    {
      offset = -1;
    }
    else
    {
      offset += written;
    }
  };

  append(R"({"name":"%s","uniq_id":"%s","avty_t":"%s")", name, uniqueId, availabilityTopic);

  if (cmdTopic)
  {
    append(R"(,"cmd_t":"%s")", cmdTopic);
  }
  if (stateTopic)
  {
    append(R"(,"stat_t":"%s")", stateTopic);
  }
  if (deviceClass)
  {
    append(R"(,"dev_cla":"%s")", deviceClass);
  }
  if (unit)
  {
    append(R"(,"unit_of_meas":"%s")", unit);
  }
  if (valueTemplate)
  {
    append(R"(,"val_tpl":"%s")", valueTemplate);
  }
  if (options)
  {
    append(R"(,"options":%s)", options);
  }
  if (min)
  {
    append(R"(,"min":%s)", min);
  }
  if (max)
  {
    append(R"(,"max":%s)", max);
  }

  append(R"(,"device":{"ids":["%s"],"name":"%.*s"}})", Config::System::IDENTIFIER,
         static_cast<int>(Config::System::NAME.size()), Config::System::NAME.data());
}

}  // namespace

MQTTClient::MQTTClient(SensorController& sensorController, IrrigationController& irrigationController)
  : sensorController_(sensorController), irrigationController_(irrigationController)
{
}

MQTTClient::~MQTTClient() = default;

auto MQTTClient::getPublishInterval() const -> uint32_t
{
  return config_.publishIntervalMs;
}

auto MQTTClient::getIrrigationController() const -> IrrigationController*
{
  return &irrigationController_;
}

auto MQTTClient::getSensorController() const -> SensorController*
{
  return &sensorController_;
}

auto MQTTClient::isConnected() const -> bool
{
  return transport_.isConnected();
}

void MQTTClient::setWifiReady(bool ready)
{
  wifiReady_ = ready;
}

void MQTTClient::requestUpdate()
{
  updateRequest_ = true;
}

auto MQTTClient::isUpdateRequested() const -> bool
{
  return updateRequest_;
}

void MQTTClient::clearUpdateRequest()
{
  updateRequest_ = false;
}

auto MQTTClient::init(const MqttConfig& config) -> bool
{
  config_ = config;
  if (not config_.enabled)
  {
    return true;
  }

  const auto* base = config_.baseTopic.data();
  formatTopic(availabilityTopic_, "%s/availability", base);
  formatTopic(stateTopic_, "%s/state", base);
  formatTopic(commandTopic_, "%s/command", base);
  formatTopic(modeCommandTopic_, "%s/mode/set", base);
  formatTopic(modeStateTopic_, "%s/mode/state", base);
  formatTopic(triggerCommandTopic_, "%s/trigger/set", base);
  formatTopic(updateCommandTopic_, "%s/update/trigger", base);
  formatTopic(intervalCommandTopic_, "%s/interval/set", base);
  formatTopic(intervalStateTopic_, "%s/interval/state", base);
  formatTopic(activityStateTopic_, "%s/activity/state", base);

  printf("[MQTTClient] Initializing MQTT integration...\n");

  if (not transport_.init(config_.clientId.data(), config_.brokerHost.data(), config_.brokerPort,
                          config_.username.data(), config_.password.data()))
  {
    printf("[MQTTClient] ERROR: Transport init failed\n");
    return false;
  }

  transport_.setOnMessage([this](std::string_view topic, std::string_view payload) -> void
                          { this->handleCommand(topic, payload); });

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

  if (isConnected())
  {
    if (needsDiscovery_)
    {
      publishDiscovery();
      needsDiscovery_ = false;
    }
    if (needsInitialPublish_)
    {
      if (hasData_)
      {
        publishSensorState(nowMs, lastData_, irrigationController_.isWatering(), true);
      }
      publishIntervalState();
      needsInitialPublish_ = false;
    }
  }
}

void MQTTClient::ensureMqtt(const uint32_t nowMs)
{
  if (not wifiReady_)
  {
    return;
  }

  if (transport_.isConnected())
  {
    return;
  }

  if ((nowMs - lastReconnectAttempt_) < Config::MQTT::RECONNECT_INTERVAL_MS)
  {
    return;
  }

  lastReconnectAttempt_ = nowMs;
  connectMqtt();
}

void MQTTClient::connectMqtt()
{
  printf("[MQTTClient] Connecting to MQTT broker...\n");
  transport_.connect(
    [this](bool success) -> void
    {
      if (success)
      {
        printf("[MQTTClient] Connected callback\n");
        publishAvailability(true);
        subscribeToCommands();
        needsDiscovery_      = true;
        needsInitialPublish_ = true;
      }
      else
      {
        printf("[MQTTClient] Connection failed callback\n");
      }
    });
}

void MQTTClient::publishSensorState(const uint32_t nowMs, const SensorData& data, const bool watering, const bool force)
{
  if (not config_.enabled)
  {
    return;
  }

  lastData_ = data;
  hasData_  = true;

  if (not isConnected())
  {
    return;
  }

  if (not force and (nowMs - lastPublish_) < config_.publishIntervalMs)
  {
    return;
  }

  std::array<char, 256> payload{};
  const auto            isLightDataValid = data.light.isValid();
  const auto            isWaterDataValid = data.water.isValid();

  (void)std::snprintf(payload.data(), payload.size(),
                      "{\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f,"
                      "\"soil_moisture\":%.2f,\"light_lux\":%.2f,\"light_available\":%s,"
                      "\"water_level\":%.2f,\"water_level_available\":%s,\"watering\":%s}",
                      data.environment.temperature, data.environment.humidity, data.environment.pressure,
                      data.soil.percentage, isLightDataValid ? data.light.lux : 0.0F,
                      isLightDataValid ? "true" : "false", isWaterDataValid ? data.water.percentage : 0.0F,
                      isWaterDataValid ? "true" : "false", watering ? "true" : "false");

  if (transport_.publish(stateTopic_.data(), payload.data()))
  {
    lastPublish_ = nowMs;
  }
}

void MQTTClient::publishDiscovery()
{
  publishSensorDiscovery("sensor", "temperature", "Temperature", "{{ value_json.temperature }}", "°C", "temperature");
  sleep_ms(50);
  publishSensorDiscovery("sensor", "humidity", "Humidity", "{{ value_json.humidity }}", "%", "humidity");
  sleep_ms(50);
  publishSensorDiscovery("sensor", "pressure", "Air Pressure", "{{ value_json.pressure }}", "hPa", "pressure");
  sleep_ms(50);
  publishSensorDiscovery("sensor", "soil", "Soil Moisture", "{{ value_json.soil_moisture }}", "%", "moisture");
  sleep_ms(50);
  publishSensorDiscovery("sensor", "light", "Ambient Light", "{{ value_json.light_lux }}", "lx", "illuminance");
  sleep_ms(50);
  publishSensorDiscovery("sensor", "water", "Water Level", "{{ value_json.water_level }}", "%");
  sleep_ms(50);
  publishSelectDiscovery();
  sleep_ms(50);
  publishButtonDiscovery();
  sleep_ms(50);
  publishUpdateTriggerDiscovery();
  sleep_ms(50);
  publishNumberDiscovery();
  sleep_ms(50);
  publishTextDiscovery();
}

void MQTTClient::publishAvailability(const bool online)
{
  (void)transport_.publish(availabilityTopic_.data(), online ? "online" : "offline", true);
}

void MQTTClient::publishSensorDiscovery(const std::string_view component, const std::string_view objectId,
                                        const std::string_view name, const std::string_view valueTemplate,
                                        const std::string_view unit, const std::string_view deviceClass)
{
  std::array<char, 128> topic{};
  (void)std::snprintf(topic.data(), topic.size(), "%s/%.*s/%.*s/config", config_.discoveryPrefix.data(),
                      static_cast<int>(component.size()), component.data(), static_cast<int>(objectId.size()),
                      objectId.data());

  std::array<char, 512> payload{};
  std::array<char, 64>  uniqueId{};
  (void)std::snprintf(uniqueId.data(), uniqueId.size(), "%s_%.*s", Config::System::IDENTIFIER,
                      static_cast<int>(objectId.size()), objectId.data());

  buildDiscoveryJson(payload, std::string(name).c_str(), uniqueId.data(), nullptr, stateTopic_.data(),
                     availabilityTopic_.data(), deviceClass.empty() ? nullptr : std::string(deviceClass).c_str(),
                     unit.empty() ? nullptr : std::string(unit).c_str(), std::string(valueTemplate).c_str());

  (void)transport_.publish(topic.data(), payload.data(), true);
}

void MQTTClient::publishSelectDiscovery()
{
  std::array<char, 128> topic{};
  (void)std::snprintf(topic.data(), topic.size(), "%s/select/%s_mode/config", config_.discoveryPrefix.data(),
                      Config::System::IDENTIFIER);

  std::array<char, 512> payload{};
  std::array<char, 64>  uniqueId{};
  (void)std::snprintf(uniqueId.data(), uniqueId.size(), "%s_mode", Config::System::IDENTIFIER);

  buildDiscoveryJson(payload, "Irrigation Mode", uniqueId.data(), modeCommandTopic_.data(), modeStateTopic_.data(),
                     availabilityTopic_.data(), nullptr, nullptr, nullptr,
                     R"(["OFF","MANUAL","TIMER","HUMIDITY","EVAPOTRANSPIRATION"])");

  (void)transport_.publish(topic.data(), payload.data(), true);
}

void MQTTClient::publishButtonDiscovery()
{
  std::array<char, 128> topic{};
  (void)std::snprintf(topic.data(), topic.size(), "%s/button/%s_trigger/config", config_.discoveryPrefix.data(),
                      Config::System::IDENTIFIER);

  std::array<char, 512> payload{};
  std::array<char, 64>  uniqueId{};
  (void)std::snprintf(uniqueId.data(), uniqueId.size(), "%s_trigger", Config::System::IDENTIFIER);

  buildDiscoveryJson(payload, "Trigger Irrigation", uniqueId.data(), triggerCommandTopic_.data(), nullptr,
                     availabilityTopic_.data());

  (void)transport_.publish(topic.data(), payload.data(), true);
}

void MQTTClient::publishUpdateTriggerDiscovery()
{
  std::array<char, 128> topic{};
  (void)std::snprintf(topic.data(), topic.size(), "%s/button/%s_update/config", config_.discoveryPrefix.data(),
                      Config::System::IDENTIFIER);

  std::array<char, 512> payload{};
  std::array<char, 64>  uniqueId{};
  (void)std::snprintf(uniqueId.data(), uniqueId.size(), "%s_update", Config::System::IDENTIFIER);

  buildDiscoveryJson(payload, "Update Sensors", uniqueId.data(), updateCommandTopic_.data(), nullptr,
                     availabilityTopic_.data());

  (void)transport_.publish(topic.data(), payload.data(), true);
}

void MQTTClient::publishNumberDiscovery()
{
  std::array<char, 128> topic{};
  (void)std::snprintf(topic.data(), topic.size(), "%s/number/%s_interval/config", config_.discoveryPrefix.data(),
                      Config::System::IDENTIFIER);

  std::array<char, 512> payload{};
  std::array<char, 64>  uniqueId{};
  (void)std::snprintf(uniqueId.data(), uniqueId.size(), "%s_interval", Config::System::IDENTIFIER);

  buildDiscoveryJson(payload, "Update Interval", uniqueId.data(), intervalCommandTopic_.data(),
                     intervalStateTopic_.data(), availabilityTopic_.data(), nullptr, "s", nullptr, nullptr, "60",
                     "86400");

  (void)transport_.publish(topic.data(), payload.data(), true);
}

void MQTTClient::publishTextDiscovery()
{
  std::array<char, 128> topic{};
  (void)std::snprintf(topic.data(), topic.size(), "%s/text/%s_activity/config", config_.discoveryPrefix.data(),
                      Config::System::IDENTIFIER);

  std::array<char, 512> payload{};
  std::array<char, 64>  uniqueId{};
  (void)std::snprintf(uniqueId.data(), uniqueId.size(), "%s_activity", Config::System::IDENTIFIER);

  buildDiscoveryJson(payload, "Activity Log", uniqueId.data(), nullptr, activityStateTopic_.data(),
                     availabilityTopic_.data());

  (void)transport_.publish(topic.data(), payload.data(), true);
}

void MQTTClient::publishIntervalState()
{
  std::array<char, 16> payload{};
  (void)std::snprintf(payload.data(), payload.size(), "%u", config_.publishIntervalMs / 1000);
  (void)transport_.publish(intervalStateTopic_.data(), payload.data(), true);
}

void MQTTClient::subscribeToCommands()
{
  (void)transport_.subscribe(modeCommandTopic_.data());
  (void)transport_.subscribe(triggerCommandTopic_.data());
  (void)transport_.subscribe(updateCommandTopic_.data());
  (void)transport_.subscribe(intervalCommandTopic_.data());
}

void MQTTClient::handleCommand(const std::string_view topic, const std::string_view payload)
{
  if (payload.empty())
  {
    return;
  }

  printf("[MQTTClient] Command received on %.*s: %.*s\n", static_cast<int>(topic.size()), topic.data(),
         static_cast<int>(payload.size()), payload.data());

  if (topic == std::string_view(modeCommandTopic_.data()))
  {
    handleModeCommand(payload);
  }
  else if (topic == std::string_view(triggerCommandTopic_.data()))
  {
    handleTriggerCommand(payload);
  }
  else if (topic == std::string_view(updateCommandTopic_.data()))
  {
    if (payload == "PRESS")
    {
      requestUpdate();
    }
  }
  else if (topic == std::string_view(intervalCommandTopic_.data()))
  {
    handleIntervalCommand(payload);
  }
}

void MQTTClient::handleModeCommand(const std::string_view payload)
{
  IrrigationMode mode = IrrigationMode::OFF;
  if (payload == "OFF")
  {
    mode = IrrigationMode::OFF;
  }
  else if (payload == "MANUAL")
  {
    mode = IrrigationMode::MANUAL;
  }
  else if (payload == "TIMER")
  {
    mode = IrrigationMode::TIMER;
  }
  else if (payload == "HUMIDITY")
  {
    mode = IrrigationMode::HUMIDITY;
  }
  else if (payload == "EVAPOTRANSPIRATION")
  {
    mode = IrrigationMode::EVAPOTRANSPIRATION;
  }
  else
  {
    return;
  }

  irrigationController_.setMode(mode);
  (void)transport_.publish(modeStateTopic_.data(), std::string(payload).c_str(), true);
}

void MQTTClient::handleTriggerCommand(const std::string_view payload)
{
  if (payload == "PRESS" and irrigationController_.getMode() == IrrigationMode::MANUAL)
  {
    const auto waterLevel = sensorController_.readWaterLevel();
    if (waterLevel.isEmpty())
    {
      printf("[MQTTClient] Trigger ignored: Water tank is empty\n");
      publishActivity("Trigger ignored: Empty tank");
      return;
    }
    irrigationController_.startWatering(Config::DEFAULT_WATERING_DURATION_MS);
  }
}

void MQTTClient::handleIntervalCommand(const std::string_view payload)
{
  uint32_t intervalSec = 0;
  const auto [ptr, ec] = std::from_chars(payload.begin(), payload.end(), intervalSec);
  if (ec == std::errc() and intervalSec >= 60 and intervalSec <= 86400)
  {
    setPublishInterval(intervalSec * 1000);
  }
}

void MQTTClient::setPublishInterval(const uint32_t intervalMs)
{
  config_.publishIntervalMs = intervalMs;
  publishIntervalState();
}

void MQTTClient::publishActivity(const std::string_view message)
{
  (void)transport_.publish(activityStateTopic_.data(), std::string(message).c_str(), true);
}
