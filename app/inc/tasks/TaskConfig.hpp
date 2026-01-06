#pragma once

#include <FreeRTOS.h>
#include <portmacrocommon.h>
#include <task.h>

#include <cstdint>

inline constexpr UBaseType_t BUTTON_TASK_PRIORITY     = tskIDLE_PRIORITY + 4;
inline constexpr UBaseType_t SENSOR_TASK_PRIORITY     = tskIDLE_PRIORITY + 2;
inline constexpr UBaseType_t NETWORK_TASK_PRIORITY    = tskIDLE_PRIORITY + 2;
inline constexpr UBaseType_t IRRIGATION_TASK_PRIORITY = tskIDLE_PRIORITY + 1;
inline constexpr UBaseType_t LED_TASK_PRIORITY        = tskIDLE_PRIORITY + 1;
inline constexpr UBaseType_t WIFI_PROV_PRIORITY       = tskIDLE_PRIORITY + 1;

inline constexpr uint16_t SENSOR_TASK_STACK     = 2048;
inline constexpr uint16_t NETWORK_TASK_STACK    = 2048;
inline constexpr uint16_t WIFI_PROV_STACK       = 2048;
inline constexpr uint16_t IRRIGATION_TASK_STACK = 1024;
inline constexpr uint16_t BUTTON_TASK_STACK     = 768;
inline constexpr uint16_t LED_TASK_STACK        = 768;
