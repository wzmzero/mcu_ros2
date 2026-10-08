#include "board.h"
#include "main.h"
#include "micro_ros.h"
#include "uart_transport.h"
#ifdef APP_ROS_USB_RNDIS
#include "usb_network.h"
#endif
#include "FreeRTOS.h"
#include "task.h"
#include <errno.h>

#if configAPPLICATION_ALLOCATED_HEAP
/* CPU-only heap/task stacks. UART RX DMA and USB packets remain in main SRAM. */
uint8_t ucHeap[configTOTAL_HEAP_SIZE] __attribute__((section(".ccm_heap"), aligned(8)));
#endif

void board_start(void)
{
#ifdef APP_ROS_USB_RNDIS
    usb_network_start();
#else
    uart_dma_init();
#endif
    /* Stack is in words: 4096 * 4 = 16 KiB. */
    if (xTaskCreate(micro_ros_task, "micro_ros", 4096, NULL, 3, NULL) != pdPASS)
        board_panic();
    vTaskStartScheduler();
    board_panic();
}
void board_panic(void) { Error_Handler(); }
void vApplicationMallocFailedHook(void) { board_panic(); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{ (void)task; (void)name; board_panic(); }
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *size)
{
    static StaticTask_t idle_tcb;
    static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
    *tcb = &idle_tcb; *stack = idle_stack; *size = configMINIMAL_STACK_SIZE;
}
void vApplicationGetTimerTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *size)
{
    static StaticTask_t timer_tcb;
    static StackType_t timer_stack[configTIMER_TASK_STACK_DEPTH];
    *tcb = &timer_tcb; *stack = timer_stack; *size = configTIMER_TASK_STACK_DEPTH;
}
/* printf must never mix text with the XRCE-DDS frames on USART1. */
int _write(int fd, char *data, int len)
{ (void)fd; (void)data; (void)len; errno = ENOSYS; return -1; }
int _read(int fd, char *data, int len)
{ (void)fd; (void)data; (void)len; errno = ENOSYS; return -1; }
