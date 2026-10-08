#ifndef MICRO_ROS_PLATFORM_H
#define MICRO_ROS_PLATFORM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct uxrCustomTransport;
typedef struct {
    const char *node_name;
    const char *node_namespace;
    size_t domain_id;
    uint32_t client_key; /* Zero retains the library's default key. */
    bool transport_framing; /* true: serial stream; false: complete XRCE datagrams. */
    const char *peer_namespace;
    bool communication_demo;
} micro_ros_platform_config_t;
/* Init runs once inside the ROS task, before the allocator and transport are used. */
void micro_ros_platform_init(void);
micro_ros_platform_config_t micro_ros_platform_config(void);
uint32_t micro_ros_platform_millis(void);
void micro_ros_platform_delay(uint32_t milliseconds);
void micro_ros_platform_panic(void);
bool micro_ros_transport_open(struct uxrCustomTransport *transport);
bool micro_ros_transport_close(struct uxrCustomTransport *transport);
size_t micro_ros_transport_write(struct uxrCustomTransport *transport, const uint8_t *buffer, size_t length, uint8_t *error);
size_t micro_ros_transport_read(struct uxrCustomTransport *transport, uint8_t *buffer, size_t length, int timeout_ms, uint8_t *error);
#endif
