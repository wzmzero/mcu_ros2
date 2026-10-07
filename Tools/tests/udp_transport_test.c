/* Host tests exercise the same UDP callbacks that ESP-IDF compiles with lwIP. */
#include "micro_ros_platform.h"
#include "udp_transport.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int bind_loopback(uint16_t *port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    assert(fd >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    assert(bind(fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    socklen_t size = sizeof(address);
    assert(getsockname(fd, (struct sockaddr *)&address, &size) == 0);
    *port = ntohs(address.sin_port);
    struct timeval timeout = {.tv_sec = 2};
    assert(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    return fd;
}
static double milliseconds(void)
{
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return now.tv_sec * 1000.0 + now.tv_nsec / 1000000.0;
}
static void local_tests(void)
{
    uint8_t error, buffer[64];
    assert(!udp_transport_configure("invalid", 8888));
    assert(!micro_ros_transport_open(NULL));
    assert(!udp_transport_configure("0.0.0.0", 8888));
    assert(!udp_transport_configure("127.0.0.1", 0));
    uint16_t port;
    int server = bind_loopback(&port);
    assert(udp_transport_configure("127.0.0.1", port));
    assert(micro_ros_transport_open(NULL));
    assert(!udp_transport_configure("127.0.0.1", port));
    const uint8_t request[] = {0x80, 0, 0x7e, 0x7d, 0xff};
    assert(micro_ros_transport_write(NULL, request, sizeof(request), &error) == sizeof(request) && !error);
    struct sockaddr_in client;
    socklen_t client_size = sizeof(client);
    assert(recvfrom(server, buffer, sizeof(buffer), 0, (struct sockaddr *)&client, &client_size) == sizeof(request));
    assert(memcmp(buffer, request, sizeof(request)) == 0); /* No serial framing/escaping. */
    uint16_t wrong_port;
    int wrong_sender = bind_loopback(&wrong_port);
    assert(sendto(wrong_sender, request, sizeof(request), 0, (struct sockaddr *)&client, client_size) == sizeof(request));
    double start = milliseconds();
    assert(micro_ros_transport_read(NULL, buffer, sizeof(buffer), 30, &error) == 0 && !error);
    assert(milliseconds() - start >= 20 && milliseconds() - start < 1000);
    assert(sendto(server, request, sizeof(request), 0, (struct sockaddr *)&client, client_size) == sizeof(request));
    assert(micro_ros_transport_read(NULL, buffer, sizeof(buffer), 1000, &error) == sizeof(request) && !error);
    assert(memcmp(buffer, request, sizeof(request)) == 0);
    uint8_t oversized[32] = {0};
    assert(sendto(server, oversized, sizeof(oversized), 0, (struct sockaddr *)&client, client_size) == sizeof(oversized));
    assert(micro_ros_transport_read(NULL, buffer, 8, 1000, &error) == 0 && error);
    assert(micro_ros_transport_read(NULL, buffer, sizeof(buffer), 0, &error) == 0 && !error);
    assert(micro_ros_transport_close(NULL));
    assert(micro_ros_transport_close(NULL));
    assert(micro_ros_transport_write(NULL, request, sizeof(request), &error) == 0 && error);
    assert(micro_ros_transport_read(NULL, buffer, sizeof(buffer), 1, &error) == 0 && error);
    assert(micro_ros_transport_open(NULL));
    assert(micro_ros_transport_close(NULL));
    close(wrong_sender); close(server);
    puts("UDP callbacks: datagram boundaries, source filter, timeout, truncation and reopen OK.");
}
static void agent_test(uint16_t port)
{
    /* XRCE GET_INFO / INFO_ACTIVITY probe, identical to test_agent.sh. */
    const uint8_t request[] = {0x80,0,0,0,2,1,8,0,0,10,0xff,0xfd,2,0,0,0};
    uint8_t response[512], error;
    assert(udp_transport_configure("127.0.0.1", port));
    for (int i = 0; i < 3; ++i) {
        assert(micro_ros_transport_open(NULL));
        assert(micro_ros_transport_write(NULL, request, sizeof(request), &error) == sizeof(request) && !error);
        size_t length = micro_ros_transport_read(NULL, response, sizeof(response), 3000, &error);
        assert(!error && length >= 14 && response[0] == 0x80 && response[4] == 6);
        assert(memcmp(response + 8, request + 8, 4) == 0 && response[12] == 0);
        assert((size_t)(response[6] | response[7] << 8) == length - 8);
        assert(micro_ros_transport_close(NULL));
    }
    puts("UDP callbacks: real Agent XRCE ping OK (3 connections).");
}
int main(int argc, char **argv)
{
    local_tests();
    if (argc == 2) {
        long port = strtol(argv[1], NULL, 10);
        assert(port > 0 && port <= 65535);
        agent_test((uint16_t)port);
    }
    return 0;
}
