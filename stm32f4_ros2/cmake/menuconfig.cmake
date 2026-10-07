find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(STM32_SDKCONFIG "${CMAKE_SOURCE_DIR}/sdkconfig" CACHE FILEPATH "STM32 Kconfig configuration")
set(_config_tool "${CMAKE_SOURCE_DIR}/scripts/menuconfig.py")
set(_config_output "${CMAKE_BINARY_DIR}/generated")
if(STM32_USB_RNDIS)
    set(_transport_seed "ROS_TRANSPORT_USB_RNDIS=y")
else()
    set(_transport_seed "ROS_TRANSPORT_UART=y")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E env PYTHONUTF8=1 ${Python3_EXECUTABLE} ${_config_tool}
    --config ${STM32_SDKCONFIG} --output ${_config_output}
    --seed ${_transport_seed} --seed ROS_DOMAIN_ID=${ROS_DOMAIN_ID}
    --seed ROS_UART_BAUD=${STM32_ROS_UART_BAUD}
    --seed USB_HSE_HZ=${STM32_USB_HSE_HZ} --seed USB_MCU_IP=${STM32_USB_MCU_IP}
    --seed USB_HOST_IP=${STM32_USB_HOST_IP} --seed ROS_AGENT_IP=${STM32_ROS_AGENT_IP}
    --seed ROS_AGENT_PORT=${STM32_ROS_AGENT_PORT}
    RESULT_VARIABLE _config_result)
if(NOT _config_result EQUAL 0)
    message(FATAL_ERROR "STM32 configuration generation failed")
endif()
include(${_config_output}/sdkconfig.cmake)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${_config_output})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${STM32_SDKCONFIG} ${CMAKE_SOURCE_DIR}/Kconfig ${_config_tool})
add_custom_target(menuconfig
    COMMAND ${CMAKE_COMMAND} -E env PYTHONUTF8=1 ${Python3_EXECUTABLE} ${_config_tool}
        --config ${STM32_SDKCONFIG} --menuconfig
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} USES_TERMINAL)
message(STATUS "STM32 transport from ${STM32_SDKCONFIG}: USB RNDIS=${STM32_USB_RNDIS}")
