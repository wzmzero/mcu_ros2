#include "udp_transport.h"
#include "micro_ros_platform.h"
#include <errno.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#include "lwip/inet.h"
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

/* Used by one ROS task. Each write/read is one whole, unframed XRCE packet. */
static int agent_socket = -1;
static struct sockaddr_in agent_address;
static bool configured;

bool udp_transport_configure(const char *agent_ip, uint16_t agent_port)
{
    if (agent_socket >= 0) return false;
    memset(&agent_address, 0, sizeof(agent_address));
    agent_address.sin_family = AF_INET;
    agent_address.sin_port = htons(agent_port);
    configured = agent_ip && agent_port &&
        inet_pton(AF_INET, agent_ip, &agent_address.sin_addr) == 1 &&
        agent_address.sin_addr.s_addr != htonl(INADDR_ANY) &&
        agent_address.sin_addr.s_addr != htonl(INADDR_BROADCAST);
    return configured;
}

bool micro_ros_transport_close(struct uxrCustomTransport *transport)
{
    (void)transport;
    if (agent_socket < 0) return true;
    int socket_to_close = agent_socket;
    agent_socket = -1;
    return close(socket_to_close) == 0;
}

bool micro_ros_transport_open(struct uxrCustomTransport *transport)
{
    if (!micro_ros_transport_close(transport) || !configured) return false;
    agent_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (agent_socket < 0) return false;
    struct timeval send_timeout = {.tv_sec = 0, .tv_usec = 200000};
    if (setsockopt(agent_socket, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout)) < 0 ||
        connect(agent_socket, (const struct sockaddr *)&agent_address, sizeof(agent_address)) < 0) {
        (void)micro_ros_transport_close(transport);
        return false;
    }
    /* Connected UDP filters packets from other IP addresses/ports. */
    return true;
}

size_t micro_ros_transport_write(struct uxrCustomTransport *transport,
    const uint8_t *buffer, size_t length, uint8_t *error)
{
    (void)transport;
    *error = 0;
    if (agent_socket < 0 || !buffer || !length || length > 65507) { *error = 1; return 0; }
    ssize_t count = send(agent_socket, buffer, length, 0);
    if (count < 0 || (size_t)count != length) { *error = 1; return 0; }
    return (size_t)count;
}

size_t micro_ros_transport_read(struct uxrCustomTransport *transport,
    uint8_t *buffer, size_t length, int timeout_ms, uint8_t *error)
{
    (void)transport;
    *error = 0;
    if (agent_socket < 0 || !buffer || !length) { *error = 1; return 0; }
    if (timeout_ms < 0) timeout_ms = 0;
    struct timeval timeout = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
    fd_set readers;
    FD_ZERO(&readers);
    FD_SET(agent_socket, &readers);
    int ready = select(agent_socket + 1, &readers, NULL, NULL, &timeout);
    if (ready == 0) return 0;
    if (ready < 0) { *error = errno == EINTR ? 0 : 1; return 0; }
    struct iovec iov = {.iov_base = buffer, .iov_len = length};
    struct msghdr message = {.msg_iov = &iov, .msg_iovlen = 1};
    ssize_t count = recvmsg(agent_socket, &message, MSG_DONTWAIT);
    if (count < 0) {
        *error = errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : 1;
        return 0;
    }
    /* Discard oversized datagrams rather than passing a partial XRCE packet. */
    if (message.msg_flags & MSG_TRUNC) { *error = 1; return 0; }
    return (size_t)count;
}
