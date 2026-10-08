#ifndef APP_LWIPOPTS_H
#define APP_LWIPOPTS_H
/* All lwIP calls belong to the USB task. No tcpip thread or socket layer. */
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define MEM_ALIGNMENT 4
#define MEM_SIZE (4 * 1024)
#define PBUF_POOL_SIZE 6
#define PBUF_POOL_BUFSIZE 1536
#define MEMP_NUM_PBUF 8
#define MEMP_NUM_UDP_PCB 2
#define MEMP_NUM_SYS_TIMEOUT 4
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ETHERNET 1
#define LWIP_ARP 1
#define LWIP_ICMP 1
#define LWIP_UDP 1
#define LWIP_TCP 0
#define LWIP_RAW 0
#define LWIP_DNS 0
#define LWIP_DHCP 0
/* Permit DHCP server requests before the host has an IPv4 source address. */
#define LWIP_IP_ACCEPT_UDP_PORT(dst_port) ((dst_port) == PP_NTOHS(67))
#define LWIP_AUTOIP 0
#define LWIP_IGMP 0
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define IP_FRAG 0
#define IP_REASSEMBLY 0
#define ETHARP_TABLE_SIZE 4
#define ARP_QUEUEING 1
#define MEMP_NUM_ARP_QUEUE 4
#define LWIP_NETIF_LINK_CALLBACK 0
#define LWIP_NETIF_STATUS_CALLBACK 0
#define LWIP_SINGLE_NETIF 1
#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 0
#define SYS_STATS 0
#define TCP_STATS 0
#define LWIP_DEBUG 0
#endif
