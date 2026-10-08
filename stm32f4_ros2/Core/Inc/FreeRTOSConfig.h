/* FreeRTOS kernel configuration for the STM32F407 micro-ROS port.
 * Kernel sources in Middlewares are under their bundled MIT license. */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include <stdint.h>
extern uint32_t SystemCoreClock;
#define configUSE_PREEMPTION 1
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configCPU_CLOCK_HZ SystemCoreClock
#define configTICK_RATE_HZ ((TickType_t)1000)
/* TinyUSB supports newer FreeRTOS as well; this kernel predates the macro. */
#define pdTICKS_TO_MS(ticks) ((uint32_t)((uint64_t)(ticks) * 1000 / configTICK_RATE_HZ))
#define configMAX_PRIORITIES 8
#define configMINIMAL_STACK_SIZE ((uint16_t)128)
#define configTOTAL_HEAP_SIZE ((size_t)(64 * 1024))
#if defined(APP_ROS_USB_RNDIS) || defined(MICRO_ROS_COMM_DEMO)
#define configAPPLICATION_ALLOCATED_HEAP 1
#endif
#define configMAX_TASK_NAME_LEN 16
#define configUSE_16_BIT_TICKS 0
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
#define configUSE_COUNTING_SEMAPHORES 1
#define configUSE_TRACE_FACILITY 0
#define configQUEUE_REGISTRY_SIZE 0
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_CO_ROUTINES 0
#define configMAX_CO_ROUTINE_PRIORITIES 2
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY 2
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH 256
#define configUSE_NEWLIB_REENTRANT 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_xTimerPendFunctionCall 1
#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY (15 << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (5 << (8 - configPRIO_BITS))
void board_panic(void);
#define configASSERT(x) do { if (!(x)) board_panic(); } while (0)
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
/* HAL uses TIM14. SysTick belongs exclusively to FreeRTOS. */
void xPortSysTickHandler(void);
#endif
