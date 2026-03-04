#ifndef MYUART_H_
#define MYUART_H_

#include "bsp_sys.h"
#include "ringbuffer.h"

#define UART2_RX_BUFFER_SIZE   1024U

extern struct rt_ringbuffer uart2_ringbuffer;


void myuart2_init(void);
void uart2_task(void);
int uart_printf(USART_TypeDef *USARTx, const char* format, ...);


#endif /* MYUART_H_ */
