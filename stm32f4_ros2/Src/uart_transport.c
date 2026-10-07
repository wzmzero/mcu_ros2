#include "uart_transport.h"
#include "board.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"

#define RX_DMA_SIZE 512U
#define RX_STREAM_SIZE 4096U
UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_rx;
static uint8_t rx_dma[RX_DMA_SIZE]; /* DMA-accessible SRAM; never place in CCM. */
static StaticStreamBuffer_t rx_control;
static uint8_t rx_storage[RX_STREAM_SIZE + 1];
static StreamBufferHandle_t rx_stream;
static uint16_t rx_last;
static volatile bool rx_fault;
volatile uint32_t uart_rx_overruns;
volatile uint32_t uart_errors;

void uart_dma_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);
    huart1.Instance = USART1;
    huart1.Init.BaudRate = APP_ROS_UART_BAUD;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
    hdma_usart1_rx.Instance = DMA2_Stream2;
    hdma_usart1_rx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode = DMA_CIRCULAR;
    hdma_usart1_rx.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_usart1_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK) Error_Handler();
    __HAL_LINKDMA(&huart1, hdmarx, hdma_usart1_rx);
    /* Same preemption priority: DMA half/full and UART idle callbacks serialize. */
    HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 5, 0);
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    rx_stream = xStreamBufferCreateStatic(RX_STREAM_SIZE, 1, rx_storage, &rx_control);
    if (!rx_stream) Error_Handler();
}
bool uart_dma_close(void)
{
    return HAL_UART_AbortReceive(&huart1) == HAL_OK;
}
bool uart_dma_open(void)
{
    (void)uart_dma_close();
    rx_last = 0;
    rx_fault = false;
    if (xStreamBufferReset(rx_stream) != pdPASS) return false;
    return HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_dma, RX_DMA_SIZE) == HAL_OK;
}
/* Callback runs on IDLE, DMA half transfer and DMA transfer complete. */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t position)
{
    if (uart != &huart1) return;
    (void)position;
    /* Read the live cursor: delayed HT/TC events may follow an IDLE event.
       Using their stale callback position would duplicate buffered bytes. */
    position = RX_DMA_SIZE - (uint16_t)__HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
    if (position == RX_DMA_SIZE) position = 0;
    BaseType_t wake = pdFALSE;
    if (position > rx_last) {
        size_t count = position - rx_last;
        if (xStreamBufferSendFromISR(rx_stream, rx_dma + rx_last, count, &wake) != count)
            ++uart_rx_overruns;
    } else if (position < rx_last) {
        size_t count = RX_DMA_SIZE - rx_last;
        if (count && xStreamBufferSendFromISR(rx_stream, rx_dma + rx_last, count, &wake) != count)
            ++uart_rx_overruns;
        if (position && xStreamBufferSendFromISR(rx_stream, rx_dma, position, &wake) != position)
            ++uart_rx_overruns;
    }
    rx_last = position;
    portYIELD_FROM_ISR(wake);
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart == &huart1) { ++uart_errors; rx_fault = true; }
}
size_t uart_dma_read(uint8_t *buffer, size_t length, int timeout_ms)
{
    if (rx_fault && !uart_dma_open()) return 0;
    return xStreamBufferReceive(rx_stream, buffer, length,
        timeout_ms > 0 ? pdMS_TO_TICKS((uint32_t)timeout_ms) : 0);
}
size_t uart_dma_write(const uint8_t *buffer, size_t length)
{
    /* Blocking TX avoids DMA reading a caller buffer after this function returns. */
    if (length > UINT16_MAX) return 0;
    uint32_t timeout = (uint32_t)(length * 10U * 1000U / 115200U) + 100U;
    return HAL_UART_Transmit(&huart1, buffer, (uint16_t)length, timeout) == HAL_OK ? length : 0;
}
void DMA2_Stream2_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_usart1_rx); }
void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart1); }
