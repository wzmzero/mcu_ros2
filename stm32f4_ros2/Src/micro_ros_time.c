#include "FreeRTOS.h"
#include "task.h"
#include <time.h>
#include <sys/time.h>
#include <errno.h>
int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
    (void)clock_id;
    if (!tp) return -1;
    TimeOut_t now;
    vTaskSetTimeOutState(&now);
    uint64_t ticks = ((uint64_t)(uint32_t)now.xOverflowCount << 32) + now.xTimeOnEntering;
    tp->tv_sec = (time_t)(ticks / configTICK_RATE_HZ);
    tp->tv_nsec = (long)((ticks % configTICK_RATE_HZ) * 1000000000ULL / configTICK_RATE_HZ);
    return 0;
}
/* Newlib time(), used by rclc action goal UUID generation, needs this syscall.
 * It supplies board uptime; ROS wall-clock timestamps use Agent time sync. */
int _gettimeofday(struct timeval *tv, void *timezone)
{
    (void)timezone;
    if (!tv) { errno = EINVAL; return -1; }
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return -1;
    tv->tv_sec = now.tv_sec;
    tv->tv_usec = now.tv_nsec / 1000;
    return 0;
}
