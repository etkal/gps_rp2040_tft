# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.24)

# Set the board type for pin and LED decisions
#set(PICO_BOARD pico)
#set(PICO_BOARD pico2)
set(PICO_BOARD pico_w)
#set(PICO_BOARD pico2_w)
#set(PICO_BOARD waveshare_rp2040_zero)
#set(PICO_BOARD seeed_xiao_rp2040)

# Use this to enable multicore support for the RP2040. On some hardware this can improve performance.
add_compile_definitions(USE_MULTICORE)

# GPS data callback delay (in milliseconds). On some hardware this is necessary in order to
# keep the display update from interfering with the received UART data (even if on another core).
# Note that after switching from UART IRQ to DMA, this does not seem to be needed.
add_compile_definitions(GPS_SEND_DATA_DELAY_MS=0)

# TFT display type - enable one of the following
#
#add_compile_definitions(DISPLAY_ILI934X)
add_compile_definitions(DISPLAY_ILI948X)
#add_compile_definitions(DISPLAY_ST7796)

# SPI device speed for the display. They seem to be able to handle 40MHz, but some boards may not be
# able to handle that speed. If you have issues, try 20MHz or 10MHz.
add_compile_definitions(DISPLAY_SPI_SPEED=40000000)

# Set the following if using Waveshare Pico-ResTouch-LCD for GPIO assignments.
#
add_compile_definitions(DISPLAY_PICO_RESTOUCH)

# ILI9341, ILI948X and ST7796 can all use 18-bit pixels (RGB666), but the default is 16-bit (RGB565).
#
#add_compile_definitions(DISPLAY_COLOUR_FORMAT=RGB666)

# Number of quadrants to use, needed for memory usage
add_compile_definitions(DISPLAY_QUADRANTS=4)

# Set the display rotation, one of R0DEG, R90DEG, R180DEG, R270DEG
add_compile_definitions(DISPLAY_ROTATION=R270DEG)

# Time zone for clock display (examples: UTC, America/New_York, Europe/London)
add_compile_definitions(TIME_ZONE=\"America/New_York\")
# DST override for clock display: AUTOMATIC, NO, or YES
add_compile_definitions(USE_DST=AUTOMATIC)
# Legacy fallback for simple numeric offsets
add_compile_definitions(GMT_OFFSET=-4.0)

# Adafruit breakout GPS modules support sending antenna status requests to the GPS device.
# This is not needed for other modules, but it is mostly harmless to enable it (can cause
# issues with some GPS modules). If you have issues, comment out the following line.
add_compile_definitions(SEND_ANTENNA_STATUS_REQUESTS) # Enable sending antenna status requests to the GPS device

# Set pico platform definitions for conditional compilation
if(PICO_BOARD STREQUAL pico_w OR PICO_BOARD STREQUAL pico2_w)
    add_compile_definitions(PLATFORM_PICO_W)
endif()
if(PICO_BOARD STREQUAL pico OR PICO_BOARD STREQUAL pico2 OR PICO_BOARD STREQUAL pico_w OR PICO_BOARD STREQUAL pico2_w)
    add_compile_definitions(PLATFORM_PICO)
endif()

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)
#include(pico_libs_import.cmake)

project(gps_tft C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(PICO_CXX_ENABLE_EXCEPTIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_compile_options(-Wall
    -Wno-format # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
    -Wno-unused-function # we have some for the docs that aren't called
    -Wno-maybe-uninitialized
)

# Power status
add_library(power_status_adc INTERFACE)
target_sources(power_status_adc INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/power_status.cpp
)
target_include_directories(power_status_adc INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src
)
target_link_libraries(power_status_adc INTERFACE
    hardware_adc
    hardware_gpio
)

add_executable(gps_tft)
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)
# generate the header file into the source tree as it is included in the RP2040 datasheet
pico_generate_pio_header(gps_tft
    ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio
    OUTPUT_DIR
    ${CMAKE_CURRENT_LIST_DIR}/generated
)

add_subdirectory(src)

target_include_directories(gps_tft PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/font
)

target_link_libraries(gps_tft
    pico_stdlib
    pico_stdio
    hardware_gpio
    hardware_pio
    hardware_spi
    hardware_dma
    power_status_adc
    pico_aon_timer
    pico_multicore
)

if(PICO_BOARD STREQUAL pico_w OR PICO_BOARD STREQUAL pico2_w)
    target_link_libraries(gps_tft
        pico_cyw43_arch_none
    )
endif()

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(gps_tft)
