#pragma once

#include "AppContext.hpp"
#include "IrrigationController.hpp"
#include "SensorController.hpp"

#include "MQTTClient.hpp"

struct SensorTaskContext
{
  SensorController*     sensorController;
  IrrigationController* irrigationController;
  AppContext*           appContext;
  MQTTClient*           mqttClient;
};

void sensorTask(void* params);
