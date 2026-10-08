# Shared firmware source manifest, included by both MCU builds.
# Application code is common; each MCU provides its own Src platform port.
get_filename_component(FIRMWARE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(MICRO_ROS_APP_SRCS "${FIRMWARE_DIR}/app/micro_ros.c" "${FIRMWARE_DIR}/app/communication_demo.c")
set(MICRO_ROS_APP_INCS "${FIRMWARE_DIR}/app" "${FIRMWARE_DIR}/core")
