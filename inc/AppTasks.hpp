#pragma once

#include "HomeAssistantClient.hpp"
#include "IrrigationController.hpp"
#include "SensorManager.hpp"
#include "WifiProvisioner.hpp"

void startAppTasks(SensorManager& sensorManager, IrrigationController& irrigationController,
                   HomeAssistantClient& haClient, WifiProvisioner& provisioner);
