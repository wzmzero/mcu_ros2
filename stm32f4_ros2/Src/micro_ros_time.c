#include "FreeRTOS.h"
#include "task.h"
#include <time.h>
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
