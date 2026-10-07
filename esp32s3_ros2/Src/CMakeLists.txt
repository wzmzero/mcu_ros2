include("${CMAKE_CURRENT_LIST_DIR}/../../firmware/cmake/firmware.cmake")
set(transport_srcs "serial_transport.c")
if(CONFIG_APP_ROS_WIFI_UDP OR CONFIG_APP_ROS_USB_RNDIS_UDP)
    set(transport_srcs "network.c" "../../firmware/transport/udp_transport.c")
endif()
if(CONFIG_APP_ROS_USB_RNDIS_UDP)
    list(APPEND transport_srcs "usb_network.c")
endif()
idf_component_register(
    SRCS ${MICRO_ROS_APP_SRCS} "board.c" "micro_ros_platform.c" ${transport_srcs}
    INCLUDE_DIRS "." "../../firmware/transport" ${MICRO_ROS_APP_INCS}
    REQUIRES micro_ros_espidf_component freertos esp_timer esp_system
             esp_driver_usb_serial_jtag esp_driver_uart
             esp_event esp_netif lwip esp_wifi nvs_flash esp_tinyusb)
# Src is the application component; there is deliberately no separate main/.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u app_main")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
# rcutils requests Annex K; picolibc must see that before any libc header.
if(CONFIG_LIBC_PICOLIBC)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE __STDC_WANT_LIB_EXT1__=1)
endif()
