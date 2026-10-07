#include "network.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#if CONFIG_APP_ROS_WIFI_UDP
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "ros_network";

static void wifi_connect(void)
{
    esp_err_t result = esp_wifi_connect();
    if (result != ESP_OK)
        ESP_LOGE(TAG, "Wi-Fi connect request failed: %s", esp_err_to_name(result));
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: %s", CONFIG_APP_ROS_WIFI_SSID);
        wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%u; reconnecting", event->reason);
        wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *event = data;
        ESP_LOGI(TAG, "Wi-Fi associated on channel %u; waiting for DHCP", event->channel);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "Wi-Fi ready: IP=" IPSTR " gateway=" IPSTR " netmask=" IPSTR,
                 IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.gw),
                 IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "micro-ROS Agent: %s:%d (UDP), domain=%d",
                 CONFIG_APP_ROS_AGENT_IP, CONFIG_APP_ROS_AGENT_PORT, CONFIG_APP_ROS_DOMAIN_ID);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW(TAG, "Wi-Fi lost its IP address");
    }
}

static esp_err_t wifi_init(void)
{
    const size_t ssid_length = strlen(CONFIG_APP_ROS_WIFI_SSID);
    const size_t password_length = strlen(CONFIG_APP_ROS_WIFI_PASSWORD);
    if (!ssid_length || ssid_length > 32 || password_length > 64) {
        ESP_LOGE(TAG, "Invalid Wi-Fi credential length; check application configuration");
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        result = nvs_flash_erase();
        if (result != ESP_OK) return result;
        result = nvs_flash_init();
    }
    if (result != ESP_OK) return result;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&config);
    if (result != ESP_OK) return result;
    result = esp_wifi_set_country_code(CONFIG_APP_ROS_WIFI_COUNTRY, true);
    if (result != ESP_OK) return result;
    ESP_LOGI(TAG, "Wi-Fi country code: %s", CONFIG_APP_ROS_WIFI_COUNTRY);
    result = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    if (result != ESP_OK) return result;
    result = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    if (result != ESP_OK) return result;
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, CONFIG_APP_ROS_WIFI_SSID, ssid_length);
    memcpy(wifi.sta.password, CONFIG_APP_ROS_WIFI_PASSWORD, password_length);
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) return result;
    /* Credentials remain in sdkconfig, not in the device's persistent NVS. */
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) return result;
    result = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    if (result != ESP_OK) return result;
    result = esp_wifi_start();
    if (result != ESP_OK) return result;
    return esp_wifi_set_ps(WIFI_PS_NONE);
}
#endif

esp_err_t app_network_init(void)
{
    esp_err_t result = esp_netif_init();
    if (result != ESP_OK) return result;
    result = esp_event_loop_create_default();
    if (result != ESP_OK) return result;
#if CONFIG_APP_ROS_WIFI_UDP
    return wifi_init();
#elif CONFIG_APP_ROS_USB_RNDIS_UDP
    return usb_network_init();
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
