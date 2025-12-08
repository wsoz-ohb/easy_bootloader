#ifndef _MYUSART_H_
#define _MYUSART_H_

#include "bsp_sys.h"

int uart_printf(UART_HandleTypeDef* huart, const char* format, ...) ;
void uart1_task(void);
void uart2_task(void);
void myusart_init(void);

#endif
