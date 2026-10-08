cmake_minimum_required(VERSION 3.22)
# WSL generates the library; Windows ESP-IDF builds the application.
set(MICROROS_PREBUILT ON)
option(MICROROS_PREPARE "Configure only, to export the WSL library build settings" OFF)
set(IDF_TARGET "esp32s3" CACHE STRING "ESP-IDF target")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/Src")
# One active configuration for all transports; menuconfig selects the mode.
set(SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/sdkconfig" CACHE FILEPATH "Project configuration" FORCE)
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults" CACHE STRING "Initial defaults" FORCE)
set(BUILD_WLAN_INTERFACE OFF CACHE BOOL "Application manages network interfaces" FORCE)
set(BUILD_ETHERNET_INTERFACE OFF CACHE BOOL "Application manages network interfaces" FORCE)
# All dependencies are vendored; no registry download is necessary.
set(ENV{IDF_COMPONENT_MANAGER} "0")
if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "Activate your Windows ESP-IDF environment first; see README.md.")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_ros2)
