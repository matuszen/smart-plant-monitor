add_library(target_utils)
target_sources(target_utils PRIVATE
    src/FlashManager.cpp
    src/Common.cpp
)
target_include_directories(target_utils PUBLIC
    inc
)
target_link_libraries(target_utils PUBLIC
    target_config
    pico_stdlib
    pico_multicore
    pico_cyw43_arch_lwip_sys_freertos
    hardware_flash
    hardware_sync
    FreeRTOS-Kernel
)
