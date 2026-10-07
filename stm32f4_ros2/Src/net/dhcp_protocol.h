#ifndef APP_DHCP_PROTOCOL_H
#define APP_DHCP_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
/* IP arrays are in network byte order. Returns zero for invalid/foreign requests.
 * One fixed lease for the computer on the USB point-to-point link. No router/DNS. */
size_t dhcp_make_reply(const uint8_t *request, size_t request_size,
    const uint8_t server[4], const uint8_t host[4], uint8_t *reply, size_t capacity);
#endif
