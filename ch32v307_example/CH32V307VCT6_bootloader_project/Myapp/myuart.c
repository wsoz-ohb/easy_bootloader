#include "myuart.h"
#include "ringbuffer.h"
#define UART2_BAUDRATE        115200U

uint8_t uart2_buffer[UART2_RX_BUFFER_SIZE];
uint32_t uart2_tick;
struct rt_ringbuffer uart2_ringbuffer;




void myuart2_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};


    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    /* PA2 -> TX */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA3 -> RX */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_DeInit(USART2);
    USART_StructInit(&USART_InitStructure);
    USART_InitStructure.USART_BaudRate = UART2_BAUDRATE;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART2, &USART_InitStructure);

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    //环形缓存区初始化
    rt_ringbuffer_init(&uart2_ringbuffer, uart2_buffer, UART2_RX_BUFFER_SIZE);
}

void USART2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        uint8_t data = (uint8_t)USART_ReceiveData(USART2);
        uart2_tick=get_uwtick();
        rt_ringbuffer_put(&uart2_ringbuffer, &data, 1);
    }
}


void uart2_task(void)
{
    if(uwtick-uart2_tick>=10)
    {
        rt_uint16_t data_len = rt_ringbuffer_data_len(&uart2_ringbuffer);
        if(data_len > 0U)
        {
            uint8_t uart2_read[UART2_RX_BUFFER_SIZE];
            rt_uint16_t read_len = rt_ringbuffer_get(&uart2_ringbuffer,
                                                     uart2_read,
                                                     (data_len > UART2_RX_BUFFER_SIZE) ? UART2_RX_BUFFER_SIZE : data_len);
            uart_printf(USART2, "%s\r\n",uart2_read);
            memset(uart2_read,0,read_len);
        }


    }
}

//串口打印重定向
int uart_printf(USART_TypeDef *USARTx, const char* format, ...) {
    char buffer[512]; // 设定一个足够大的缓冲区
    va_list args;
    va_start(args, format);

    // 使用vsprintf进行格式化输出
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (len < 0) {
        return -1;  // 格式化失败
    }

    // 通过UART发送格式化后的字符串
    for(int i=0;i<len;i++)
    {
       while (!(USARTx->STATR & USART_FLAG_TXE)); // 等待发送缓冲区为空,避免堵塞
       USART_SendData(USARTx, buffer[i]);
    }
    return len;
}
