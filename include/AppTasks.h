#pragma once

#include "HomeAssistantClient.h"
#include "IrrigationController.h"
#include "SensorManager.h"
#include "WifiProvisioner.h"

void startAppTasks(SensorManager& sensorManager, IrrigationController& irrigationController,
                   HomeAssistantClient& haClient, WifiProvisioner& provisioner);
