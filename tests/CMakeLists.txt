macro(create_test_executable name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE
        app_core
        target_network
        target_sensors
        target_utils
        target_config
        pico_stdlib
        pico_cyw43_arch_lwip_sys_freertos
        hardware_adc
        hardware_i2c
        hardware_gpio
        hardware_pwm
        FreeRTOS-Kernel-Heap4
    )
    pico_enable_stdio_usb(${name} 1)
    pico_enable_stdio_uart(${name} 0)
    pico_add_extra_outputs(${name})
endmacro()

create_test_executable(bh1750Test bh1750Test.cpp)
create_test_executable(bme280Test bme280Test.cpp)
create_test_executable(hw103Test hw103Test.cpp)
create_test_executable(ledTest ledTest.cpp)
create_test_executable(se054Test se054Test.cpp)
create_test_executable(waterLevelTest waterLevelTest.cpp)
create_test_executable(waterPumpTest waterPumpTest.cpp)
