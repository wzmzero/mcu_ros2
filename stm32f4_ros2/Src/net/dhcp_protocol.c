/* DHCP wire layout follows mcu_test's dhcp_server.c; validate full TLVs before
 * replying, including selected server and requested address. No lwIP dependency. */
#include "dhcp_protocol.h"
#include <stdbool.h>
#include <string.h>

size_t dhcp_make_reply(const uint8_t *request, size_t size,
    const uint8_t server[4], const uint8_t host[4], uint8_t *reply, size_t capacity)
{
    static const uint8_t cookie[] = {99, 130, 83, 99};
    static const uint8_t zero[] = {0, 0, 0, 0};
    uint8_t type = 0;
    bool ended = false, has_requested = false;
    if (!request || !server || !host || !reply || size < 244 || capacity < 300 ||
        request[0] != 1 || request[1] != 1 || request[2] != 6 ||
        memcmp(request + 236, cookie, 4) || memcmp(request + 24, zero, 4)) return 0;
    for (size_t i = 240; i < size;) {
        uint8_t option = request[i++];
        if (option == 255) { ended = true; break; }
        if (option == 0) continue;
        if (i == size) return 0;
        uint8_t length = request[i++];
        if (length > size - i) return 0;
        if (option == 53) {
            if (length != 1 || type) return 0;
            type = request[i];
        } else if (option == 54) {
            if (length != 4 || memcmp(request + i, server, 4)) return 0;
        } else if (option == 50) {
            if (length != 4) return 0;
            has_requested = true;
            /* A DISCOVER can suggest a previous address; still offer our lease. */
            if (type == 3 && memcmp(request + i, host, 4)) return 0;
        }
        i += length;
    }
    if (!ended || (type != 1 && type != 3)) return 0;
    /* Options can occur in any order, so validate requested IP once type is known. */
    if (type == 3 && has_requested) {
        for (size_t i = 240; i < size;) {
            uint8_t option = request[i++];
            if (option == 255) break;
            if (!option) continue;
            uint8_t length = request[i++];
            if (option == 50 && memcmp(request + i, host, 4)) return 0;
            i += length;
        }
    }
    if (type == 3 && !has_requested && memcmp(request + 12, zero, 4) &&
        memcmp(request + 12, host, 4)) return 0;
    memset(reply, 0, 300);
    reply[0] = 2; reply[1] = 1; reply[2] = 6;
    memcpy(reply + 4, request + 4, 4); /* xid */
    memcpy(reply + 10, request + 10, 2); /* flags */
    memcpy(reply + 16, host, 4);
    memcpy(reply + 20, server, 4);
    memcpy(reply + 28, request + 28, 16);
    memcpy(reply + 236, cookie, 4);
    uint8_t *p = reply + 240;
    *p++ = 53; *p++ = 1; *p++ = type == 1 ? 2 : 5; /* OFFER/ACK */
    *p++ = 54; *p++ = 4; memcpy(p, server, 4); p += 4;
    *p++ = 51; *p++ = 4; *p++ = 0; *p++ = 0; *p++ = 14; *p++ = 16; /* 3600 s */
    *p++ = 1; *p++ = 4; *p++ = 255; *p++ = 255; *p++ = 255; *p++ = 0;
    *p = 255;
    return 300; /* BOOTP minimum size with zero padding */
}
