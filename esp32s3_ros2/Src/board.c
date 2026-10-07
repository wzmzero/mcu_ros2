#include "micro_ros.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

void app_main(void)
{
    /* ESP-IDF task stack sizes are bytes, unlike the STM32 FreeRTOS port. */
    if (xTaskCreatePinnedToCore(micro_ros_task, "micro_ros", 16384, NULL, 5, NULL, 0) != pdPASS)
        abort();
}
