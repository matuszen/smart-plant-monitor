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

include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/FreeRTOS_Kernel_import.cmake)

add_subdirectory(config)
add_subdirectory(utils)
add_subdirectory(sensors)
add_subdirectory(web)

add_subdirectory(app)
add_subdirectory(network)

add_subdirectory(tests)
