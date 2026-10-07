#include "usb_network.h"
#include "board.h"
#include "micro_ros_platform.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "tusb.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "dhcp_protocol.h"
#include <uxr/client/config.h>
#include <string.h>

#define XRCE_MTU UXR_CONFIG_CUSTOM_TRANSPORT_MTU
#define XRCE_QUEUE_DEPTH 4
#define ETH_QUEUE_DEPTH 4
_Static_assert(XRCE_MTU <= 1472, "XRCE MTU must fit an unfragmented Ethernet UDP packet");
typedef struct { uint32_t generation; uint16_t size; uint8_t data[XRCE_MTU]; } xrce_packet_t;
typedef struct { uint16_t size; uint8_t data[CFG_TUD_NET_MTU]; } eth_packet_t;
volatile usb_network_stats_t usb_network_stats;
/* CPU-only allocations and task stacks. USB packets stay in main SRAM. */
uint8_t ucHeap[configTOTAL_HEAP_SIZE] __attribute__((section(".ccm_heap"), aligned(8)));
static struct netif usb_netif;
static struct udp_pcb *agent_pcb;
static ip_addr_t agent_ip;
static QueueHandle_t tx_queue, rx_queue;
static StaticQueue_t tx_control, rx_control;
static uint8_t tx_storage[XRCE_QUEUE_DEPTH * sizeof(xrce_packet_t)];
static uint8_t rx_storage[XRCE_QUEUE_DEPTH * sizeof(xrce_packet_t)];
static eth_packet_t ethernet_queue[ETH_QUEUE_DEPTH];
static unsigned eth_head, eth_count;
static volatile bool mounted, transport_open;
static volatile uint32_t generation;
void usb_descriptors_init(void);

u32_t sys_now(void) { return HAL_GetTick(); }
uint32_t tusb_time_millis_api(void) { return HAL_GetTick(); }
void OTG_FS_IRQHandler(void) { tud_int_handler(0); }
void tud_mount_cb(void) { mounted = true; }
void tud_umount_cb(void) { mounted = false; }
void tud_suspend_cb(bool remote_wakeup) { (void)remote_wakeup; mounted = false; }
void tud_resume_cb(void) { mounted = tud_mounted(); }
void tud_network_init_cb(void) { eth_head = eth_count = 0; }
bool tud_network_default_link_state_cb(void) { return true; }

static err_t usb_linkoutput(struct netif *netif, struct pbuf *packet)
{
    (void)netif;
    if (!mounted || packet->tot_len > CFG_TUD_NET_MTU || eth_count == ETH_QUEUE_DEPTH) {
        ++usb_network_stats.ethernet_dropped;
        return ERR_MEM;
    }
    eth_packet_t *copy = &ethernet_queue[(eth_head + eth_count) % ETH_QUEUE_DEPTH];
    copy->size = packet->tot_len;
    if (pbuf_copy_partial(packet, copy->data, copy->size, 0) != copy->size) return ERR_BUF;
    ++eth_count;
    return ERR_OK;
}
uint16_t tud_network_xmit_cb(uint8_t *destination, void *reference, uint16_t size)
{
    const eth_packet_t *packet = reference;
    memcpy(destination, packet->data, size);
    return size;
}
bool tud_network_recv_cb(const uint8_t *source, uint16_t size)
{
    /* Callback runs inside tud_task_ext(), never the USB IRQ. */
    if (size >= 14 && size <= CFG_TUD_NET_MTU) {
        struct pbuf *packet = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (packet && pbuf_take(packet, source, size) == ERR_OK) {
            ++usb_network_stats.ethernet_rx;
            if (usb_netif.input(packet, &usb_netif) != ERR_OK) pbuf_free(packet);
        } else {
            if (packet) pbuf_free(packet);
            ++usb_network_stats.ethernet_dropped;
        }
    } else ++usb_network_stats.ethernet_dropped;
    tud_network_recv_renew();
    return true;
}
static err_t usb_netif_init(struct netif *netif)
{
    netif->name[0] = 'u'; netif->name[1] = 's';
    netif->mtu = 1500; netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, tud_network_mac_address, 6);
    netif->hwaddr[5] ^= 1;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    netif->output = etharp_output; netif->linkoutput = usb_linkoutput;
    return ERR_OK;
}
static void receive_xrce(void *arg, struct udp_pcb *pcb, struct pbuf *packet,
                         const ip_addr_t *address, u16_t port)
{
    (void)arg; (void)pcb;
    xrce_packet_t copy;
    copy.generation = generation; copy.size = packet->tot_len;
    if (!transport_open || !ip_addr_cmp(address, &agent_ip) || port != APP_ROS_AGENT_PORT ||
        !copy.size || copy.size > XRCE_MTU ||
        pbuf_copy_partial(packet, copy.data, copy.size, 0) != copy.size ||
        xQueueSend(rx_queue, &copy, 0) != pdTRUE) ++usb_network_stats.udp_dropped;
    else {
        ++usb_network_stats.udp_rx;
        UBaseType_t depth = uxQueueMessagesWaiting(rx_queue);
        if (depth > usb_network_stats.rx_queue_peak) usb_network_stats.rx_queue_peak = depth;
    }
    pbuf_free(packet);
}
static void receive_dhcp(void *arg, struct udp_pcb *pcb, struct pbuf *packet,
                         const ip_addr_t *address, u16_t port)
{
    (void)arg; (void)address;
    uint8_t request[576], reply[300];
    if (port != 68 || packet->tot_len > sizeof(request)) { pbuf_free(packet); return; }
    size_t size = pbuf_copy_partial(packet, request, sizeof(request), 0);
    pbuf_free(packet);
    ip4_addr_t host;
    if (!ip4addr_aton(APP_USB_HOST_IP, &host)) board_panic();
    size = dhcp_make_reply(request, size, (const uint8_t *)&netif_ip4_addr(&usb_netif)->addr,
                          (const uint8_t *)&host.addr, reply, sizeof(reply));
    if (!size) return;
    packet = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
    if (!packet) return;
    if (pbuf_take(packet, reply, size) == ERR_OK &&
        udp_sendto_if(pcb, packet, IP_ADDR_BROADCAST, 68, &usb_netif) == ERR_OK)
        ++usb_network_stats.dhcp_replies;
    pbuf_free(packet);
}
static void network_task(void *arg)
{
    (void)arg;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {.Pin = GPIO_PIN_11 | GPIO_PIN_12, .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_VERY_HIGH, .Alternate = GPIO_AF10_OTG_FS};
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 5, 0);
    usb_descriptors_init();
    lwip_init();
    ip4_addr_t ip, mask, gateway;
    if (!ip4addr_aton(APP_USB_MCU_IP, &ip) || !ip4addr_aton("255.255.255.0", &mask)) board_panic();
    ip4_addr_set_zero(&gateway);
    if (!netif_add(&usb_netif, &ip, &mask, &gateway, NULL, usb_netif_init, ethernet_input)) board_panic();
    netif_set_default(&usb_netif); netif_set_up(&usb_netif);
    if (!ipaddr_aton(APP_ROS_AGENT_IP, &agent_ip)) board_panic();
    agent_pcb = udp_new();
    struct udp_pcb *dhcp = udp_new();
    if (!agent_pcb || !dhcp || udp_bind(agent_pcb, IP_ADDR_ANY, 0) != ERR_OK ||
        udp_connect(agent_pcb, &agent_ip, APP_ROS_AGENT_PORT) != ERR_OK ||
        udp_bind(dhcp, IP_ADDR_ANY, 67) != ERR_OK) board_panic();
    udp_recv(agent_pcb, receive_xrce, NULL); udp_recv(dhcp, receive_dhcp, NULL);
    /* PA9 remains USART1 TX in UART builds; USB does not use VBUS sensing. */
    const tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    if (!tusb_init(0, &init)) board_panic();
    bool was_mounted = false;
    uint32_t last_stats = HAL_GetTick();
    xrce_packet_t copy;
    for (;;) {
        tud_task_ext(0, false);
        bool up = mounted;
        if (up != was_mounted) {
            was_mounted = up; ++usb_network_stats.link_changes;
            usb_network_stats.mounted = up;
            if (up) netif_set_link_up(&usb_netif);
            else { netif_set_link_down(&usb_netif); eth_head = eth_count = 0; }
        }
        sys_check_timeouts();
        /* A bounded burst prevents ROS/idle starvation under a USB flood. */
        for (unsigned i = 0; i < XRCE_QUEUE_DEPTH && xQueueReceive(tx_queue, &copy, 0) == pdTRUE; ++i) {
            if (!up || !transport_open || copy.generation != generation) { ++usb_network_stats.udp_dropped; continue; }
            struct pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, copy.size, PBUF_RAM);
            if (packet && pbuf_take(packet, copy.data, copy.size) == ERR_OK && udp_send(agent_pcb, packet) == ERR_OK)
                ++usb_network_stats.udp_tx;
            else ++usb_network_stats.udp_dropped;
            if (packet) pbuf_free(packet);
        }
        if (up && eth_count && tud_network_can_xmit(ethernet_queue[eth_head].size)) {
            tud_network_xmit(&ethernet_queue[eth_head], ethernet_queue[eth_head].size);
            eth_head = (eth_head + 1) % ETH_QUEUE_DEPTH; --eth_count;
            ++usb_network_stats.ethernet_tx;
        }
        if ((uint32_t)(HAL_GetTick() - last_stats) >= 1000) {
            last_stats = HAL_GetTick();
            usb_network_stats.usb_stack_free_words = uxTaskGetStackHighWaterMark(NULL);
            usb_network_stats.min_heap_free = xPortGetMinimumEverFreeHeapSize();
        }
        vTaskDelay(1);
    }
}
void usb_network_start(void)
{
    tx_queue = xQueueCreateStatic(XRCE_QUEUE_DEPTH, sizeof(xrce_packet_t), tx_storage, &tx_control);
    rx_queue = xQueueCreateStatic(XRCE_QUEUE_DEPTH, sizeof(xrce_packet_t), rx_storage, &rx_control);
    if (!tx_queue || !rx_queue || xTaskCreate(network_task, "usb_net", 1536, NULL, 4, NULL) != pdPASS) board_panic();
}
bool micro_ros_transport_open(struct uxrCustomTransport *transport)
{
    (void)transport;
    taskENTER_CRITICAL(); ++generation; transport_open = true; taskEXIT_CRITICAL();
    return mounted;
}
bool micro_ros_transport_close(struct uxrCustomTransport *transport)
{
    (void)transport;
    taskENTER_CRITICAL(); transport_open = false; ++generation; taskEXIT_CRITICAL();
    return true;
}
size_t micro_ros_transport_write(struct uxrCustomTransport *transport, const uint8_t *buffer,
                                 size_t size, uint8_t *error)
{
    (void)transport;
    *error = 0;
    if (!transport_open || !mounted || !buffer || !size || size > XRCE_MTU) { *error = 1; return 0; }
    xrce_packet_t copy = {.generation = generation, .size = (uint16_t)size};
    memcpy(copy.data, buffer, size);
    if (xQueueSend(tx_queue, &copy, pdMS_TO_TICKS(200)) != pdTRUE) { *error = 1; return 0; }
    UBaseType_t depth = uxQueueMessagesWaiting(tx_queue);
    if (depth > usb_network_stats.tx_queue_peak) usb_network_stats.tx_queue_peak = depth;
    return size;
}
size_t micro_ros_transport_read(struct uxrCustomTransport *transport, uint8_t *buffer,
                                size_t size, int timeout_ms, uint8_t *error)
{
    (void)transport;
    *error = 0;
    if (!transport_open || !buffer || !size) { *error = 1; return 0; }
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = timeout_ms > 0 ? pdMS_TO_TICKS((uint32_t)timeout_ms) : 0;
    xrce_packet_t copy;
    for (;;) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        TickType_t remaining = elapsed < timeout ? timeout - elapsed : 0;
        if (xQueueReceive(rx_queue, &copy, remaining) != pdTRUE) return 0;
        if (copy.generation != generation) { if (elapsed >= timeout) return 0; continue; }
        if (copy.size > size) { *error = 1; return 0; }
        memcpy(buffer, copy.data, copy.size);
        return copy.size;
    }
}
void usb_network_clock_config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON; oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLM = HSE_VALUE / 1000000; oscillator.PLL.PLLN = 336;
    oscillator.PLL.PLLP = RCC_PLLP_DIV2; oscillator.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) board_panic();
    clocks.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK; clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV4; clocks.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_5) != HAL_OK) board_panic();
}
