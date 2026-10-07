#include "dhcp_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t server[] = {192,168,8,1}, host[] = {192,168,8,2};
static uint8_t request[576], response[304];
static size_t request_init(uint8_t type)
{
    memset(request, 0, sizeof(request));
    request[0] = request[1] = 1; request[2] = 6;
    request[4] = 0x12; request[5] = 0x34;
    request[10] = 0x80; request[28] = 2; request[29] = 0xF4;
    request[236] = 99; request[237] = 130; request[238] = 83; request[239] = 99;
    request[240] = 53; request[241] = 1; request[242] = type; request[243] = 255;
    return 244;
}
static size_t make(size_t size)
{ return dhcp_make_reply(request, size, server, host, response, 300); }
int main(void)
{
    size_t size = request_init(1);
    memset(response, 0xA5, sizeof(response));
    assert(make(size) == 300 && response[242] == 2);
    assert(!memcmp(response + 16, host, 4) && !memcmp(response + 4, request + 4, 4));
    assert(!memcmp(response + 28, request + 28, 16) && response[300] == 0xA5);
    for (size_t n = 0; n < size; ++n) assert(make(n) == 0);
    assert(dhcp_make_reply(request, size, server, host, response, 299) == 0);
    request_init(3);
    request[243] = 50; request[244] = 4; memcpy(request + 245, host, 4); request[249] = 255;
    assert(make(250) == 300 && response[242] == 5);
    request[248] = 9; assert(make(250) == 0); /* foreign lease */
    /* Requested address before message type must have the same validation. */
    request_init(3); request[240] = 50; request[241] = 4; memcpy(request + 242, host, 4);
    request[246] = 53; request[247] = 1; request[248] = 3; request[249] = 255;
    assert(make(250) == 300); request[245] = 9; assert(make(250) == 0);
    request_init(1); request[243] = 54; request[244] = 4;
    memcpy(request + 245, server, 4); request[249] = 255; assert(make(250) == 300);
    request[248] = 9; assert(make(250) == 0); /* selected another server */
    request_init(1); request[243] = 50; request[244] = 255; assert(make(250) == 0);
    request_init(1); request[243] = 53; request[244] = 1; request[245] = 3; request[246] = 255;
    assert(make(247) == 0); /* duplicate message type */
    request_init(3); memcpy(request + 12, host, 4); assert(make(244) == 300);
    request[15] = 9; assert(make(244) == 0);
    request_init(1); request[24] = 1; assert(make(244) == 0); /* relayed request */
    /* Deterministic malformed TLV mutations exercise bounds under sanitizers. */
    unsigned state = 1;
    for (unsigned i = 0; i < 100000; ++i) {
        size = request_init(1);
        state = state * 1664525u + 1013904223u;
        size = 240 + (state % 337);
        for (size_t j = 240; j < size; ++j) {
            state = state * 1664525u + 1013904223u; request[j] = state >> 24;
        }
        memset(response, 0xA5, sizeof(response));
        (void)make(size);
        assert(response[300] == 0xA5 && response[303] == 0xA5);
    }
    puts("DHCP offer/ack, TLV bounds, foreign server/lease and 100000 malformed requests passed.");
    return 0;
}
