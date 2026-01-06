add_library(app_core)
target_sources(app_core PRIVATE
    src/controllers/IrrigationController.cpp
    src/controllers/SensorController.cpp
    src/controllers/ConnectionController.cpp
    src/Hooks.cpp
)
target_include_directories(app_core PUBLIC
    inc
    inc/controllers
    inc/tasks
)
target_link_libraries(app_core PUBLIC
    target_sensors
    target_utils
    target_config
    target_network
    target_web
    pico_stdlib
    FreeRTOS-Kernel
)

add_executable(smart_plant_monitor
    src/tasks/TaskEntry.cpp
    src/main.cpp
    src/tasks/ButtonTask.cpp
    src/tasks/LedTask.cpp
    src/tasks/NetworkTask.cpp
    src/tasks/SensorTask.cpp
    src/tasks/WifiTask.cpp
)
target_include_directories(smart_plant_monitor PRIVATE
    inc
    inc/controllers
    inc/tasks
)
target_link_libraries(smart_plant_monitor PRIVATE
    app_core
    target_network
    target_web
    target_utils
    target_config
    pico_stdlib
    pico_cyw43_arch_lwip_sys_freertos
    pico_lwip_mdns
    hardware_adc
    hardware_i2c
    hardware_gpio
    hardware_pwm
    FreeRTOS-Kernel-Heap4
)

pico_enable_stdio_usb(smart_plant_monitor 1)
pico_enable_stdio_uart(smart_plant_monitor 0)

pico_add_extra_outputs(smart_plant_monitor)
