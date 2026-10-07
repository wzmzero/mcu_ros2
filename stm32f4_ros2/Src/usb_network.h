#ifndef APP_USB_NETWORK_H
#define APP_USB_NETWORK_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t mounted, link_changes, ethernet_rx, ethernet_tx, ethernet_dropped;
    uint32_t udp_rx, udp_tx, udp_dropped, dhcp_replies;
    uint32_t tx_queue_peak, rx_queue_peak, usb_stack_free_words, min_heap_free;
} usb_network_stats_t;
extern volatile usb_network_stats_t usb_network_stats;
void usb_network_start(void);
void usb_network_clock_config(void);
#endif
