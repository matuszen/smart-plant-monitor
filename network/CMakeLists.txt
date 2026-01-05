add_library(target_network)
target_sources(target_network PRIVATE
    src/dhcpserver.c
    src/MQTTClient.cpp
    src/WifiDriver.cpp
    src/MqttTransport.cpp
)
target_include_directories(target_network PUBLIC
    inc
)
target_link_libraries(target_network PUBLIC
    app_core
    target_utils
    target_config
    pico_stdlib
    pico_lwip_mqtt
    pico_cyw43_arch_lwip_sys_freertos
)
