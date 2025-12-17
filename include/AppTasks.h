#pragma once

#include "HomeAssistantClient.h"
#include "IrrigationController.h"
#include "SensorManager.h"

void startAppTasks(SensorManager& sensorManager, IrrigationController& irrigationController,
                   HomeAssistantClient& haClient);
