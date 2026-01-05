add_library(target_sensors)
target_sources(target_sensors PRIVATE
    src/EnvironmentalSensor.cpp
    src/LightSensor.cpp
    src/SoilMoistureSensor.cpp
    src/WaterLevelSensor.cpp
)
target_include_directories(target_sensors PUBLIC
    inc
)
target_link_libraries(target_sensors PUBLIC
    target_utils
    pico_stdlib
    hardware_adc
    hardware_i2c
    hardware_gpio
)
