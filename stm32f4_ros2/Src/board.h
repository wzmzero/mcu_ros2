#ifndef BOARD_H
#define BOARD_H
#include "stm32f4xx_hal.h"
extern UART_HandleTypeDef huart1;
void board_start(void);
void board_panic(void);
#endif
