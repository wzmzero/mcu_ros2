#ifndef MICRO_ROS_UDP_TRANSPORT_H
#define MICRO_ROS_UDP_TRANSPORT_H
#include <stdbool.h>
#include <stdint.h>
/* Configure once before registering the custom transport. Numeric IPv4 only. */
bool udp_transport_configure(const char *agent_ip, uint16_t agent_port);
#endif
