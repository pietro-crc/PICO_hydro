# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

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
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(PICO_hydro C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
set(PICO_MBEDTLS_CONFIG_FILE "hydro_mbedtls_config.h")
set(PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS 5000)
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(PICO_hydro
        PICO_hydro.cpp
        src/app/hydro_controller.cpp
        src/cloud/google_sheets_logger.cpp
        src/drivers/esp8266_at.cpp
        src/drivers/lcd1602_i2c.cpp
        src/drivers/relay.cpp
        src/drivers/ws2812_strip.cpp
        src/sensors/ds18b20_sensor.cpp
        src/sensors/dht_sensor.cpp
        src/sensors/flow_meter.cpp
        src/sensors/level_sensor.cpp
        src/sensors/tds_sensor.cpp
        src/sensors/veml7700_sensor.cpp
        src/ui/lcd_carousel.cpp
        )

pico_set_program_name(PICO_hydro "PICO_hydro")
pico_set_program_version(PICO_hydro "1.0")

# Generate PIO header
pico_generate_pio_header(PICO_hydro ${CMAKE_CURRENT_LIST_DIR}/pio/ws2812.pio)

# Keep UART pins free for project hardware. Logs are exposed over USB CDC.
pico_enable_stdio_uart(PICO_hydro 0)
pico_enable_stdio_usb(PICO_hydro 1)

target_compile_definitions(PICO_hydro PRIVATE
        PICO_STACK_SIZE=0x1000)

# Add the standard library to the build
# hardware_adc serve per leggere il sensore TDS su GP26 / ADC0.
target_link_libraries(PICO_hydro
        pico_stdlib
        pico_mbedtls
        hardware_adc
        hardware_i2c
        hardware_pio
        hardware_uart
        hardware_watchdog)

# Add the standard include files to the build
target_include_directories(PICO_hydro PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/include/mbedtls_port
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(PICO_hydro)
