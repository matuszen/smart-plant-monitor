#pragma once

#include "AppContext.hpp"
#include "ConnectionController.hpp"

#include "MQTTClient.hpp"

struct WifiTaskContext
{
  ConnectionController* provisioner;
  MQTTClient*           mqttClient;
  AppContext*           appContext;
};

void wifiProvisionTask(void* params);
