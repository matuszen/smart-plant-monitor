#pragma once

#include "ConnectionController.hpp"
#include "IrrigationController.hpp"

#include "MQTTClient.hpp"

#include <portmacrocommon.h>

void startAppTasks(IrrigationController& irrigationController, MQTTClient& mqttClient,
                   ConnectionController& provisioner);
