# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_BOARD pico2_w CACHE STRING "Board type")
set(PICO_PLATFORM rp2350)

cmake_minimum_required(VERSION 3.13...3.27)

set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/ext/pico-sdk)
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/ext/FreeRTOS-Kernel)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(smart_plant_monitor C CXX ASM)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/src
)
target_compile_definitions(freertos_config INTERFACE projCOVERAGE_TEST=0)
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/FreeRTOS_Kernel_import.cmake)

set(SOURCES
    src/main.cpp
    src/EnvironmentalSensor.cpp
    src/SensorManager.cpp
    src/IrrigationController.cpp
    src/WaterLevelSensor.cpp
    src/LightSensor.cpp
    src/MQTTClient.cpp
    src/SoilMoistureSensor.cpp
    src/AppTasks.cpp
    src/ConnectionManager.cpp
    src/FlashManager.cpp
    libs/src/dhcpserver.c
)

add_executable(smart_plant_monitor ${SOURCES})

target_link_libraries(smart_plant_monitor
    pico_stdlib
    pico_multicore
    pico_cyw43_arch_lwip_sys_freertos
    pico_lwip_mqtt
    pico_lwip_mdns
    hardware_adc
    hardware_i2c
    hardware_gpio
    hardware_pwm
    freertos_config
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)

pico_enable_stdio_usb(smart_plant_monitor 1)
pico_enable_stdio_uart(smart_plant_monitor 0)

pico_add_extra_outputs(smart_plant_monitor)
