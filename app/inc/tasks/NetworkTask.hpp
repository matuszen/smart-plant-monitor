#pragma once

#include "AppContext.hpp"

#include "MQTTClient.hpp"

struct NetworkTaskContext
{
  MQTTClient* mqttClient;
  AppContext* appContext;
};

void networkTask(void* params);
