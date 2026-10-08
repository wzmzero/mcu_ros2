/* RNDIS/lwIP adaptation based on mcu_test's platform_usb.c. USB owns the PHY. */
#include "network.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "esp_event.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "lwip/inet.h"
#if CONFIG_APP_ROS_USB_DHCP_SERVER
#include "dhcpserver/dhcpserver.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "device/usbd_pvt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !CFG_TUD_ECM_RNDIS || CFG_TUD_NCM || CFG_TUD_CDC
#error "USB RNDIS requires ECM/RNDIS enabled, NCM and CDC disabled; check menuconfig."
#endif

typedef struct { uint16_t length; uint8_t data[]; } usb_packet_t;
static esp_netif_t *netif;
static QueueHandle_t tx_queue;
static volatile bool mounted, drain_posted;
uint8_t tud_network_mac_address[6];
static char serial_number[13];
static const char *TAG = "ros_usb_net";
static const char *strings[] = {"\x09\x04", "Espressif", "ESP32-S3 micro-ROS RNDIS", serial_number, "RNDIS"};
static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = 64, .idVendor = 0x303A, .idProduct = 0x4008, .bcdDevice = 0x0100,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1,
};
static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_RNDIS_DESC_LEN, 0, 250),
    TUD_RNDIS_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64),
};
/* Request Windows' inbox RNDIS driver through Microsoft OS 1.0 descriptors. */
static const uint8_t ms_compatible_id[] = {
    40,0,0,0, 0,1, 4,0, 1,0,0,0,0,0,0,0,
    0,1, 'R','N','D','I','S',0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,
};
uint16_t const *tinyusb_descriptor_string_override(uint8_t index, uint16_t langid)
{
    (void)langid;
    static const uint16_t os[] = {0x0312, 'M','S','F','T','1','0','0',1};
    return index == 0xEE ? os : NULL;
}
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request)
{
    if (request->bRequest != 1 || request->wIndex != 4 ||
        request->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR || !request->bmRequestType_bit.direction)
        return false;
    return stage != CONTROL_STAGE_SETUP ||
        tud_control_xfer(rhport, request, (void *)ms_compatible_id, sizeof(ms_compatible_id));
}

static void free_rx(void *ctx, void *buffer) { (void)ctx; free(buffer); }
static esp_err_t transmit(void *ctx, void *buffer, size_t length)
{
    (void)ctx;
    if (!mounted || length > CFG_TUD_NET_MTU) return ESP_FAIL;
    usb_packet_t *packet = malloc(sizeof(*packet) + length);
    if (!packet) return ESP_ERR_NO_MEM;
    packet->length = (uint16_t)length;
    memcpy(packet->data, buffer, length);
    if (xQueueSend(tx_queue, &packet, 0) != pdTRUE) { free(packet); return ESP_ERR_NO_MEM; }
    return ESP_OK;
}

/* TinyUSB APIs below run only in TinyUSB's task, never in lwIP's task. */
static void drain_tx(void *arg)
{
    (void)arg;
    usb_packet_t *packet;
    if (!mounted) {
        while (xQueueReceive(tx_queue, &packet, 0) == pdTRUE) free(packet);
    } else if (xQueuePeek(tx_queue, &packet, 0) == pdTRUE && tud_network_can_xmit(packet->length)) {
        if (xQueueReceive(tx_queue, &packet, 0) == pdTRUE) tud_network_xmit(packet, packet->length);
    }
    drain_posted = false;
}
uint16_t tud_network_xmit_cb(uint8_t *destination, void *ref, uint16_t length)
{
    usb_packet_t *packet = ref;
    memcpy(destination, packet->data, length);
    free(packet);
    return length;
}
bool tud_network_recv_cb(const uint8_t *source, uint16_t length)
{
    if (netif && length && length <= CFG_TUD_NET_MTU) {
        void *copy = malloc(length);
        if (copy) {
            memcpy(copy, source, length);
            /* esp-netif ethernet input releases this buffer through free_rx. */
            (void)esp_netif_receive(netif, copy, length, NULL);
        }
    }
    tud_network_recv_renew();
    return true;
}
void tud_network_init_cb(void) {}
bool tud_network_default_link_state_cb(void) { return true; }
static void usb_event(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    if (event->id == TINYUSB_EVENT_ATTACHED) {
        mounted = true;
        ESP_LOGI(TAG, "RNDIS USB host attached");
    } else if (event->id == TINYUSB_EVENT_DETACHED) {
        mounted = false;
        ESP_LOGW(TAG, "RNDIS USB host detached");
    }
}
#if CONFIG_APP_ROS_USB_DHCP_SERVER
static void host_ip_assigned(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id;
    const ip_event_assigned_ip_to_client_t *event = data;
    if (event->esp_netif == netif)
        ESP_LOGI(TAG, "USB host DHCP address: " IPSTR, IP2STR(&event->ip));
}
#endif
static void network_task(void *arg)
{
    (void)arg;
    bool link_applied = false;
    for (;;) {
        bool connected = mounted;
        if (connected != link_applied) {
            link_applied = connected;
            if (connected) esp_netif_action_connected(netif, NULL, 0, NULL);
            else esp_netif_action_disconnected(netif, NULL, 0, NULL);
        }
        if (uxQueueMessagesWaiting(tx_queue) && !drain_posted) {
            drain_posted = true;
            usbd_defer_func(drain_tx, NULL, false);
        }
        TickType_t delay = pdMS_TO_TICKS(2);
        vTaskDelay(delay ? delay : 1);
    }
}
esp_err_t usb_network_init(void)
{
    uint8_t mac[6];
    esp_err_t result = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (result != ESP_OK) return result;
    mac[0] |= 2u;
    memcpy(tud_network_mac_address, mac, sizeof(mac));
    /* RNDIS reports the host NIC's MAC; MCU lwIP must use a different MAC. */
    tud_network_mac_address[5] ^= 1u;
    snprintf(serial_number, sizeof(serial_number), "%02X%02X%02X%02X%02X%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const esp_netif_ip_info_t ip = {
        .ip.addr = ESP_IP4TOADDR(192,168,7,1),
        .netmask.addr = ESP_IP4TOADDR(255,255,255,0), .gw.addr = 0,
    };
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base.flags = ESP_NETIF_FLAG_AUTOUP; /* MCU has a static address on this direct link. */
#if CONFIG_APP_ROS_USB_DHCP_SERVER
    base.flags |= ESP_NETIF_DHCP_SERVER;
#endif
    base.ip_info = &ip;
    base.if_key = "USB_RNDIS"; base.if_desc = "micro-ROS USB network"; base.route_prio = 10;
    base.get_ip_event = IP_EVENT_CUSTOM_GOT_IP; base.lost_ip_event = IP_EVENT_CUSTOM_LOST_IP;
    const esp_netif_driver_ifconfig_t driver = {
        .handle = &netif, .transmit = transmit, .driver_free_rx_buffer = free_rx,
    };
    const esp_netif_config_t config = {.base = &base, .driver = &driver, .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH};
    tx_queue = xQueueCreate(8, sizeof(usb_packet_t *));
    if (!tx_queue) return ESP_ERR_NO_MEM;
    netif = esp_netif_new(&config);
    if (!netif) return ESP_ERR_NO_MEM;
    result = esp_netif_set_mac(netif, mac);
    if (result != ESP_OK) return result;
#if CONFIG_APP_ROS_USB_DHCP_SERVER
    /* The SDK requires at least two addresses in a configured DHCP pool. */
    dhcps_lease_t lease = {
        .enable = true,
        .start_ip.addr = ESP_IP4TOADDR(192,168,7,2),
        .end_ip.addr = ESP_IP4TOADDR(192,168,7,3),
    };
    result = esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_REQUESTED_IP_ADDRESS,
                                  &lease, sizeof(lease));
    if (result != ESP_OK) return result;
    dhcps_offer_t disabled = 0;
    result = esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS,
                                  &disabled, sizeof(disabled));
    if (result != ESP_OK) return result;
    result = esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                                  &disabled, sizeof(disabled));
    if (result != ESP_OK) return result;
    result = esp_event_handler_register(IP_EVENT, IP_EVENT_ASSIGNED_IP_TO_CLIENT, host_ip_assigned, NULL);
    if (result != ESP_OK) return result;
#endif
    esp_netif_action_start(netif, NULL, 0, NULL);
    tinyusb_config_t usb = TINYUSB_DEFAULT_CONFIG(usb_event);
    usb.descriptor.device = &device_descriptor;
    usb.descriptor.string = strings;
    usb.descriptor.string_count = sizeof(strings) / sizeof(strings[0]);
    usb.descriptor.full_speed_config = configuration_descriptor;
    result = tinyusb_driver_install(&usb);
    if (result != ESP_OK) return result;
    ESP_LOGI(TAG, "RNDIS ready: MCU=192.168.7.1/24, Agent=%s:%d (UDP), host DHCP=%s",
             CONFIG_APP_ROS_AGENT_IP, CONFIG_APP_ROS_AGENT_PORT,
#if CONFIG_APP_ROS_USB_DHCP_SERVER
             "192.168.7.2-3"
#else
             "disabled"
#endif
    );
    if (xTaskCreate(network_task, "usb_net", 3072, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
