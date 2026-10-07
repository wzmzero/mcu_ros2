#ifndef UART_DMA_H
#define UART_DMA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void uart_dma_init(void);
bool uart_dma_open(void);
bool uart_dma_close(void);
size_t uart_dma_write(const uint8_t *buffer, size_t length);
size_t uart_dma_read(uint8_t *buffer, size_t length, int timeout_ms);
extern volatile uint32_t uart_rx_overruns;
extern volatile uint32_t uart_errors;
#endif
