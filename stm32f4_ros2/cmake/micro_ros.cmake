include("${CMAKE_CURRENT_LIST_DIR}/../../firmware/cmake/firmware.cmake")

# Kconfig is the only source of application and transport settings.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_sdkconfig "${CMAKE_SOURCE_DIR}/sdkconfig")
set(_config_tool "${CMAKE_SOURCE_DIR}/scripts/menuconfig.py")
set(_config_output "${CMAKE_BINARY_DIR}/generated")
execute_process(COMMAND ${CMAKE_COMMAND} -E env PYTHONUTF8=1 ${Python3_EXECUTABLE} ${_config_tool}
    --config ${_sdkconfig} --output ${_config_output} RESULT_VARIABLE _config_result)
if(NOT _config_result EQUAL 0)
    message(FATAL_ERROR "STM32 configuration generation failed")
endif()
include(${_config_output}/sdkconfig.cmake)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${_config_output})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${_sdkconfig} ${CMAKE_SOURCE_DIR}/Src/Kconfig.projbuild ${_config_tool})
add_custom_target(menuconfig
    COMMAND ${CMAKE_COMMAND} -E env PYTHONUTF8=1 ${Python3_EXECUTABLE} ${_config_tool}
        --config ${_sdkconfig} --menuconfig
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} USES_TERMINAL)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE APP_ROS_DOMAIN_ID=${CONFIG_ROS_DOMAIN_ID})
message(STATUS "STM32 transport: USB RNDIS=${CONFIG_ROS_TRANSPORT_USB_RNDIS}")

set(MICRO_ROS_DISTRO "jazzy" CACHE STRING "micro-ROS distribution")
set(_microros_default "${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/micro_ros/lib/${MICRO_ROS_DISTRO}")
# Migrate only the old default cached path; preserve explicit custom overrides.
if(MICROROS_ROOT STREQUAL "${CMAKE_SOURCE_DIR}/micro_ros_stm32cubemx_utils/microros_static_library/libmicroros"
   OR MICROROS_ROOT STREQUAL "${FIRMWARE_DIR}/lib/stm32f4/${MICRO_ROS_DISTRO}"
   OR MICROROS_ROOT STREQUAL "${CMAKE_SOURCE_DIR}/lib/micro_ros/${MICRO_ROS_DISTRO}")
    set(MICROROS_ROOT "${_microros_default}" CACHE PATH "Generated micro-ROS static library" FORCE)
endif()
set(MICROROS_ROOT "${_microros_default}" CACHE PATH "Generated micro-ROS static library")
if(NOT EXISTS "${MICROROS_ROOT}/libmicroros.a" OR NOT EXISTS "${MICROROS_ROOT}/include/rcl/rcl.h")
    message(FATAL_ERROR "Generate micro-ROS first: bash scripts/build_micro_ros.sh <ROS_DISTRO>. See README.md.")
endif()
set(RTOS "${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source")
add_library(microros STATIC IMPORTED)
set_target_properties(microros PROPERTIES IMPORTED_LOCATION "${MICROROS_ROOT}/libmicroros.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MICROROS_ROOT}/include")
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/Src ${MICRO_ROS_APP_INCS} ${RTOS}/include ${RTOS}/portable/GCC/ARM_CM4F)
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
if(CONFIG_ROS_TRANSPORT_UART)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Src/uart_transport.c)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE APP_ROS_UART_BAUD=${CONFIG_ROS_UART_BAUD})
else()
    set(_tiny "${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/TinyUSB")
    set(_lwip "${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LwIP/src")
    set(_net "${CMAKE_SOURCE_DIR}/Src/net")
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${_net} ${_lwip}/include
        ${_tiny}/src ${_tiny}/lib/networking)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE APP_ROS_USB_RNDIS=1
        APP_USB_MCU_IP="${CONFIG_USB_MCU_IP}" APP_USB_HOST_IP="${CONFIG_USB_HOST_IP}"
        APP_ROS_AGENT_IP="${CONFIG_ROS_AGENT_IP}" APP_ROS_AGENT_PORT=${CONFIG_ROS_AGENT_PORT})
    target_compile_definitions(stm32cubemx INTERFACE HSE_VALUE=${CONFIG_USB_HSE_HZ}U)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/Src/usb_network.c ${CMAKE_SOURCE_DIR}/Src/usb_descriptors.c
        ${_net}/dhcp_protocol.c
        ${_tiny}/src/tusb.c ${_tiny}/src/common/tusb_fifo.c
        ${_tiny}/src/device/usbd.c ${_tiny}/src/class/net/ecm_rndis_device.c
        ${_tiny}/src/portable/synopsys/dwc2/dcd_dwc2.c ${_tiny}/src/portable/synopsys/dwc2/dwc2_common.c
        ${_tiny}/lib/networking/rndis_reports.c ${_lwip}/netif/ethernet.c)
    foreach(_source init def inet_chksum ip mem memp netif pbuf stats sys timeouts udp)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${_lwip}/core/${_source}.c)
    endforeach()
    foreach(_source etharp icmp ip4 ip4_addr)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${_lwip}/core/ipv4/${_source}.c)
    endforeach()
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
