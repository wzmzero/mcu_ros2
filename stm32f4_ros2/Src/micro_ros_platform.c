#include "micro_ros_platform.h"
#include "micro_ros_memory.h"
#include "board.h"
#ifndef APP_ROS_USB_RNDIS
#include "uart_transport.h"
#endif
#include "FreeRTOS.h"
#include "task.h"
#include <rcutils/allocator.h>

void micro_ros_platform_init(void)
{
    rcutils_allocator_t allocator = rcutils_get_zero_initialized_allocator();
    allocator.allocate = microros_allocate;
    allocator.deallocate = microros_deallocate;
    allocator.reallocate = microros_reallocate;
    allocator.zero_allocate = microros_zero_allocate;
    if (!rcutils_set_default_allocator(&allocator)) board_panic();
}
micro_ros_platform_config_t micro_ros_platform_config(void)
{
#ifdef APP_ROS_USB_RNDIS
    uint32_t key = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
    /* A new XRCE identity per boot avoids reusing the Agent's old DDS writers
     * and reliable stream state. USB already supplies the RNG's 48 MHz clock. */
    __HAL_RCC_RNG_CLK_ENABLE();
    RNG->CR = RNG_CR_RNGEN;
    uint32_t start = HAL_GetTick();
    while (!(RNG->SR & RNG_SR_DRDY) && (uint32_t)(HAL_GetTick() - start) < 10u)
        vTaskDelay(1);
    if ((RNG->SR & RNG_SR_DRDY) && !(RNG->SR & (RNG_SR_CECS | RNG_SR_SECS))) key ^= RNG->DR;
    RNG->CR = 0;
    __HAL_RCC_RNG_CLK_DISABLE();
    if (!key) key = 1;
#else
    uint32_t key = 0;
#endif
    return (micro_ros_platform_config_t){
        .node_name = "stm32f407", .node_namespace = "/stm32", .domain_id = APP_ROS_DOMAIN_ID,
        .client_key = key,
#ifdef APP_ROS_USB_RNDIS
        .transport_framing = false,
#else
        .transport_framing = true,
#endif
        .peer_namespace = "/esp32s3",
#ifdef CONFIG_ROS_COMM_DEMO
        .communication_demo = true,
#endif
    };
}
uint32_t micro_ros_platform_millis(void) { return HAL_GetTick(); }
void micro_ros_platform_delay(uint32_t milliseconds)
{
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    if (milliseconds && !ticks) ticks = 1;
    vTaskDelay(ticks);
}
void micro_ros_platform_panic(void) { board_panic(); }

#ifndef APP_ROS_USB_RNDIS
bool micro_ros_transport_open(struct uxrCustomTransport *t)
{ (void)t; return uart_dma_open(); }
bool micro_ros_transport_close(struct uxrCustomTransport *t)
{ (void)t; return uart_dma_close(); }
size_t micro_ros_transport_write(struct uxrCustomTransport *t, const uint8_t *data, size_t size, uint8_t *error)
{
    (void)t;
    size_t written = uart_dma_write(data, size);
    *error = written == size ? 0 : 1;
    return written;
}
size_t micro_ros_transport_read(struct uxrCustomTransport *t, uint8_t *data, size_t size, int timeout, uint8_t *error)
{
    (void)t;
    uint32_t failures = uart_errors, overruns = uart_rx_overruns;
    size_t count = uart_dma_read(data, size, timeout);
    *error = (failures == uart_errors && overruns == uart_rx_overruns) ? 0 : 1;
    return count;
}
#endif
