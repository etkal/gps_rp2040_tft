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
if(EXISTS ${picoVscode})
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

# Enable the following to use the debugprobe for debugging
#
#set(PICO_DEBUGPROBE 1)

# Some LED modules use RGB ordering for the WS2812 LEDs
#
#add_compile_definitions(WS2812_LED_IS_RGB)

# Run the GPS processing on the second core if desired. The gpsmon implementation cannot
# utilize this as the wifi stack uses the primary core.
#
#add_compile_definitions(GPS_ON_CORE_1)

# Run the display on the second core. Alternative for the above.
#
add_compile_definitions(DISPLAY_ON_CORE_1)

# TFT display type - enable one of the following
#
add_compile_definitions(DISPLAY_ILI934X)
#add_compile_definitions(DISPLAY_ILI948X)
#add_compile_definitions(DISPLAY_ST7796)

# SPI device speed for the display. They seem to be able to handle 40MHz, but some boards may not be
# able to handle that speed. If you have issues, try 20MHz or 10MHz.
#
#add_compile_definitions(DISPLAY_SPI_SPEED=40000000)

# Set the following if using Waveshare Pico-ResTouch-LCD for GPIO assignments.
#
#add_compile_definitions(DISPLAY_PICO_RESTOUCH)

# ILI9341, ILI948X and ST7796 can all use 18-bit pixels (RGB666), but the default is 16-bit (RGB565).
#
#add_compile_definitions(DISPLAY_COLOUR_FORMAT=RGB666)

# Number of quadrants to use, needed for memory usage
#
add_compile_definitions(DISPLAY_QUADRANTS=4)

# Set the display rotation, one of R0DEG, R90DEG, R180DEG, R270DEG
#
add_compile_definitions(DISPLAY_ROTATION=R270DEG)

# Time zone for clock display (examples: UTC, America/New_York, Europe/London)
#
add_compile_definitions(TIME_ZONE=\"America/New_York\")
# DST override for clock display: AUTOMATIC, NO, or YES
add_compile_definitions(USE_DST=AUTOMATIC)
# Legacy fallback for simple numeric offsets
add_compile_definitions(GMT_OFFSET=-4.0)

# Adafruit breakout GPS modules support sending antenna status requests to the GPS device.
# This is not needed for other modules, but it is mostly harmless to enable it (can cause
# issues with some GPS modules). If you have issues, comment out the following line.
# setting to 0 indicates not to repeat, only send once (after a delay), any other value
# repeats at that interval (in seconds).
#
add_compile_definitions(ANTENNA_STATUS_REQUEST_REPEAT_SECONDS=0)

# Enable echo to UART1
#
add_compile_definitions(ECHO_TO_UART1)

# Privacy: obscure the location information in the GPS data
#
#add_compile_definitions(OBSCURE_LOCATION)

# Enable display of vsys voltage and power source. Pico has a resistor network built into ADC3 (GPIO29)
# but you can implement with an external network for other boards. In all cases set ADC_REF_ADJUST to 1.0f
# and measure the actual voltage to determine the correct adjustment factor (actual/reported). The variance
# is due to resistor tolerance, as well as the 3.3v reference voltage no being exact. If you are using
# a battery, you may need to account for the voltage drop across the Schottky diode in order to present
# a decent approximation of the actual battery voltage, you will need to measure on your device.
#
add_compile_definitions(DISPLAY_VSYS_VOLTAGE)
#add_compile_definitions(ADC_GPIO_PIN=26) # Override if not pico / pico_w
add_compile_definitions(ADC_REF_ADJUST=1.0f) # ADC reference voltage adjustment ratio
add_compile_definitions(SCHOTTKY_ADJUST=0.20f) # Schottky diode voltage drop
add_compile_definitions(PICO_POWER_SAMPLE_COUNT=10) # Number of samples to take for power measurement

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
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(PICO_CXX_ENABLE_EXCEPTIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_compile_options(-Wall
    -Wno-format # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
    -Wno-unused-function # we have some for the docs that aren't called
    -Wno-maybe-uninitialized
)

if(PICO_DEBUGPROBE)
    add_compile_definitions(PICO_DEBUGPROBE)
    # set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g3")
    # set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g3")
    # Set stdout to use UART1 for debugprobe
    add_compile_definitions(gps_oled PRIVATE
        PICO_DEFAULT_UART=1
        PICO_DEFAULT_UART_TX_PIN=4
        PICO_DEFAULT_UART_RX_PIN=5
    )
endif()

add_executable(gps_tft)
pico_enable_stdio_usb(${PROJECT_NAME} 1)
if(PICO_DEBUGPROBE)
    pico_enable_stdio_uart(${PROJECT_NAME} 1) # for debugprobe
endif()

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
    hardware_adc
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
