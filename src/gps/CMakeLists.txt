#
# Required source files
#

target_sources(${PROJECT_NAME} PUBLIC
    gps_uart.cpp
    gps.cpp
    gpsdevice.cpp
)

if(ENABLE_WIFI)
    target_sources(${PROJECT_NAME} PUBLIC
        gps_gpsd.cpp
    )
endif()
