include("${CMAKE_CURRENT_LIST_DIR}/../../firmware/cmake/firmware.cmake")
set(MICRO_ROS_DISTRO "jazzy" CACHE STRING "micro-ROS distribution")
set(_microros_default "${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/micro_ros/lib/${MICRO_ROS_DISTRO}")
# Migrate only the old default cached path; preserve explicit custom overrides.
if(MICROROS_ROOT STREQUAL "${CMAKE_SOURCE_DIR}/micro_ros_stm32cubemx_utils/microros_static_library/libmicroros"
   OR MICROROS_ROOT STREQUAL "${FIRMWARE_DIR}/lib/stm32f4/${MICRO_ROS_DISTRO}"
   OR MICROROS_ROOT STREQUAL "${CMAKE_SOURCE_DIR}/lib/micro_ros/${MICRO_ROS_DISTRO}")
    set(MICROROS_ROOT "${_microros_default}" CACHE PATH "Generated micro-ROS static library" FORCE)
endif()
set(MICROROS_ROOT "${_microros_default}" CACHE PATH "Generated micro-ROS static library")
set(ROS_DOMAIN_ID 0 CACHE STRING "Must match the ROS_DOMAIN_ID in WSL")
if(NOT ROS_DOMAIN_ID MATCHES "^[0-9]+$" OR ROS_DOMAIN_ID GREATER 232)
    message(FATAL_ERROR "ROS_DOMAIN_ID must be between 0 and 232")
endif()
if(NOT EXISTS "${MICROROS_ROOT}/libmicroros.a" OR NOT EXISTS "${MICROROS_ROOT}/include/rcl/rcl.h")
    message(FATAL_ERROR "Generate micro-ROS first: bash scripts/build_micro_ros.sh <ROS_DISTRO>. See README.md.")
endif()
set(RTOS "${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source")
add_library(microros STATIC IMPORTED)
set_target_properties(microros PROPERTIES IMPORTED_LOCATION "${MICROROS_ROOT}/libmicroros.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MICROROS_ROOT}/include")
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/Src ${MICRO_ROS_APP_INCS} ${RTOS}/include ${RTOS}/portable/GCC/ARM_CM4F)
include("${CMAKE_CURRENT_LIST_DIR}/usb_network.cmake")
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/Src/board.c
    ${MICRO_ROS_APP_SRCS}
    ${CMAKE_SOURCE_DIR}/Src/micro_ros_platform.c
    ${CMAKE_SOURCE_DIR}/Src/micro_ros_memory.c
    ${CMAKE_SOURCE_DIR}/Src/micro_ros_time.c
    ${CMAKE_SOURCE_DIR}/Core/Src/stm32f4xx_hal_timebase_tim.c
    ${RTOS}/tasks.c ${RTOS}/queue.c ${RTOS}/list.c ${RTOS}/timers.c
    ${RTOS}/event_groups.c ${RTOS}/stream_buffer.c
    ${RTOS}/portable/GCC/ARM_CM4F/port.c
    ${RTOS}/portable/MemMang/heap_4.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c)
if(NOT STM32_USB_RNDIS)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Src/uart_transport.c)
endif()
target_link_libraries(${CMAKE_PROJECT_NAME} microros)
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>)
file(GLOB _cubeclt_bins "C:/ST/STM32CubeCLT_*/STM32CubeProgrammer/bin")
find_program(STM32_PROGRAMMER_CLI STM32_Programmer_CLI PATHS ${_cubeclt_bins})
if(STM32_PROGRAMMER_CLI)
    add_custom_target(flash_swd
        COMMAND ${STM32_PROGRAMMER_CLI} -c port=SWD -w $<TARGET_FILE:${CMAKE_PROJECT_NAME}> -v -rst
        DEPENDS ${CMAKE_PROJECT_NAME} USES_TERMINAL)
endif()
