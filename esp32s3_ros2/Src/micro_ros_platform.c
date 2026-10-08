#include "micro_ros_platform.h"
#include "serial_transport.h"
#include "network.h"
#include "udp_transport.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static uint32_t client_key;
void micro_ros_platform_init(void)
{
#if CONFIG_APP_ROS_WIFI_UDP || CONFIG_APP_ROS_USB_RNDIS_UDP
    if (!udp_transport_configure(CONFIG_APP_ROS_AGENT_IP, CONFIG_APP_ROS_AGENT_PORT)) abort();
    ESP_ERROR_CHECK(app_network_init());
#else
    ESP_ERROR_CHECK(serial_transport_init());
#endif
    client_key = esp_random();
    if (!client_key) client_key = 1;
}
micro_ros_platform_config_t micro_ros_platform_config(void)
{
    return (micro_ros_platform_config_t){
        .node_name = "esp32s3", .node_namespace = "/esp32s3",
        .domain_id = CONFIG_APP_ROS_DOMAIN_ID, .client_key = client_key,
        .peer_namespace = "/stm32",
#if CONFIG_APP_ROS_COMM_DEMO
        .communication_demo = true,
#endif
#if CONFIG_APP_ROS_WIFI_UDP || CONFIG_APP_ROS_USB_RNDIS_UDP
        .transport_framing = false,
#else
        .transport_framing = true,
#endif
    };
}
uint32_t micro_ros_platform_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
void micro_ros_platform_delay(uint32_t milliseconds)
{
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    if (milliseconds && !ticks) ticks = 1;
    vTaskDelay(ticks);
}
void micro_ros_platform_panic(void) { abort(); }
