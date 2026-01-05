#include "MqttTransport.hpp"

#include <lwip/apps/mqtt.h>
#include <lwip/dns.h>
#include <lwip/err.h>
#include <lwip/ip4_addr.h>
#include <lwip/ip_addr.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

MqttTransport::~MqttTransport()
{
  disconnect();
  if (client_ != nullptr)
  {
    mqtt_client_free(client_);
    client_ = nullptr;
  }
}

auto MqttTransport::init(const char* const clientId, const char* const host, const uint16_t port,
                         const char* const user, const char* const pass) -> bool
{
  clientId_ = clientId;
  host_     = host;
  port_     = port;
  user_     = user;
  pass_     = pass;

  if (client_ == nullptr)
  {
    client_ = mqtt_client_new();
  }

  return client_ != nullptr;
}

void MqttTransport::connect(ConnectCallback cb)
{
  if (client_ == nullptr)
  {
    if (cb)
    {
      cb(false);
    }
    return;
  }

  if (connected_)
  {
    if (cb)
    {
      cb(true);
    }
    return;
  }

  connectCb_ = std::move(cb);

  ip_addr_t  targetAddr;
  const auto err = dns_gethostbyname(
    host_.c_str(), &targetAddr,
    [](const char* name, const ip_addr_t* ipaddr, void* callback_arg) -> void
    {
      auto* self = static_cast<MqttTransport*>(callback_arg);
      if (ipaddr == nullptr)
      {
        printf("[MqttTransport] DNS resolution failed for %s\n", name);
        if (self->connectCb_)
        {
          self->connectCb_(false);
        }
        return;
      }

      mqtt_connect_client_info_t ci{};
      ci.client_id   = self->clientId_.c_str();
      ci.client_user = self->user_.empty() ? nullptr : self->user_.c_str();
      ci.client_pass = self->pass_.empty() ? nullptr : self->pass_.c_str();
      ci.keep_alive  = 60;
      ci.will_topic  = nullptr;
      ci.will_msg    = nullptr;
      ci.will_qos    = 0;
      ci.will_retain = 0;

      mqtt_client_connect(self->client_, ipaddr, self->port_, &MqttTransport::mqttConnectionCb, self, &ci);
    },
    this);

  if (err == ERR_OK)
  {
    mqtt_connect_client_info_t ci{};
    ci.client_id   = clientId_.c_str();
    ci.client_user = user_.empty() ? nullptr : user_.c_str();
    ci.client_pass = pass_.empty() ? nullptr : pass_.c_str();
    ci.keep_alive  = 60;

    mqtt_client_connect(client_, &targetAddr, port_, &MqttTransport::mqttConnectionCb, this, &ci);
  }
  else if (err != ERR_INPROGRESS)
  {
    printf("[MqttTransport] DNS request failed: %d\n", err);
    if (connectCb_)
    {
      connectCb_(false);
    }
  }
}

void MqttTransport::disconnect()
{
  if (client_ != nullptr and connected_)
  {
    mqtt_disconnect(client_);
  }
  connected_ = false;
}

auto MqttTransport::publish(const char* const topic, const char* const payload, const bool retain) -> bool
{
  if (not connected_ or client_ == nullptr)
  {
    return false;
  }

  const auto err = mqtt_publish(client_, topic, payload, std::strlen(payload), 0, retain ? 1 : 0, nullptr, nullptr);
  return err == ERR_OK;
}

auto MqttTransport::subscribe(const char* const topic) -> bool
{
  if (not connected_ or client_ == nullptr)
  {
    return false;
  }

  const auto err = mqtt_sub_unsub(client_, topic, 0, nullptr, nullptr, 1);
  return err == ERR_OK;
}

void MqttTransport::setOnMessage(MessageCallback cb)
{
  messageCb_ = std::move(cb);
  if (client_ != nullptr)
  {
    mqtt_set_inpub_callback(client_, &MqttTransport::mqttIncomingPublishCb, &MqttTransport::mqttIncomingDataCb, this);
  }
}

auto MqttTransport::isConnected() const -> bool
{
  return connected_;
}

void MqttTransport::mqttConnectionCb(mqtt_client_t* client, void* arg, mqtt_connection_status_t status)
{
  auto* self = static_cast<MqttTransport*>(arg);
  if (status == MQTT_CONNECT_ACCEPTED)
  {
    printf("[MqttTransport] Connected\n");
    self->connected_ = true;
    if (self->connectCb_)
    {
      self->connectCb_(true);
    }

    if (self->messageCb_)
    {
      mqtt_set_inpub_callback(client, &MqttTransport::mqttIncomingPublishCb, &MqttTransport::mqttIncomingDataCb, self);
    }
  }
  else
  {
    printf("[MqttTransport] Connection failed: %d\n", status);
    self->connected_ = false;
    if (self->connectCb_)
    {
      self->connectCb_(false);
    }
  }
}

void MqttTransport::mqttIncomingPublishCb(void* arg, const char* topic, uint32_t tot_len)
{
  auto* self           = static_cast<MqttTransport*>(arg);
  self->incomingTopic_ = topic;
  self->incomingPayload_.clear();
  self->incomingPayload_.reserve(tot_len);
}

void MqttTransport::mqttIncomingDataCb(void* arg, const uint8_t* data, uint16_t len, uint8_t flags)
{
  auto* self = static_cast<MqttTransport*>(arg);
  self->incomingPayload_.append(reinterpret_cast<const char*>(data), len);

  if (flags & MQTT_DATA_FLAG_LAST)
  {
    if (self->messageCb_)
    {
      self->messageCb_(self->incomingTopic_, self->incomingPayload_);
    }
  }
}
