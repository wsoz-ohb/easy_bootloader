#include "myusart.h"

extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern UART_HandleTypeDef huart3;
extern DMA_HandleTypeDef hdma_usart3_rx;

uint8_t uart1_rx_dmabuffer[128];
uint8_t uart1_read_buffer[129];
struct rt_ringbuffer uart1_ringbuffer_struct;
rt_uint8_t uart1_ringbuffer[128];

uint8_t uart3_rx_dmabuffer[1024];
struct rt_ringbuffer uart3_ringbuffer_struct;
rt_uint8_t uart3_ringbuffer[1024];
static volatile uint8_t uart_ringbuffer_ready;

void myusart_init(void)
{
    uart_ringbuffer_ready = 0U;
    rt_ringbuffer_init(&uart1_ringbuffer_struct,
                       uart1_ringbuffer,
                       sizeof(uart1_ringbuffer));
    rt_ringbuffer_init(&uart3_ringbuffer_struct,
                       uart3_ringbuffer,
                       sizeof(uart3_ringbuffer));
    uart_ringbuffer_ready = 1U;

    (void)HAL_UART_DMAStop(&huart1);
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart1,
                                       uart1_rx_dmabuffer,
                                       sizeof(uart1_rx_dmabuffer));
    __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);

    (void)HAL_UART_DMAStop(&huart3);
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart3,
                                       uart3_rx_dmabuffer,
                                       sizeof(uart3_rx_dmabuffer));
    __HAL_DMA_DISABLE_IT(&hdma_usart3_rx, DMA_IT_HT);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    struct rt_ringbuffer *ringbuffer;
    uint8_t *dma_buffer;
    uint32_t dma_buffer_size;
    DMA_HandleTypeDef *dma_handle;

    if (huart->Instance == USART1)
    {
        ringbuffer = &uart1_ringbuffer_struct;
        dma_buffer = uart1_rx_dmabuffer;
        dma_buffer_size = sizeof(uart1_rx_dmabuffer);
        dma_handle = &hdma_usart1_rx;
    }
    else if (huart->Instance == USART3)
    {
        ringbuffer = &uart3_ringbuffer_struct;
        dma_buffer = uart3_rx_dmabuffer;
        dma_buffer_size = sizeof(uart3_rx_dmabuffer);
        dma_handle = &hdma_usart3_rx;
    }
    else
    {
        return;
    }

    (void)HAL_UART_DMAStop(huart);
    if (uart_ringbuffer_ready == 0U)
    {
        (void)HAL_UARTEx_ReceiveToIdle_DMA(huart, dma_buffer, dma_buffer_size);
        __HAL_DMA_DISABLE_IT(dma_handle, DMA_IT_HT);
        return;
    }
    if ((size > 0U) &&
        (rt_ringbuffer_put(ringbuffer, dma_buffer, size) != size))
    {
        uart_printf(&huart1, "UART RX buffer overflow\r\n");
    }
    (void)HAL_UARTEx_ReceiveToIdle_DMA(huart, dma_buffer, dma_buffer_size);
    __HAL_DMA_DISABLE_IT(dma_handle, DMA_IT_HT);
}

void uart1_task(void)
{
    uint16_t data_size = rt_ringbuffer_data_len(&uart1_ringbuffer_struct);

    if (data_size > 0U)
    {
        if (data_size > sizeof(uart1_read_buffer))
        {
            data_size = sizeof(uart1_read_buffer);
        }
        (void)rt_ringbuffer_get(&uart1_ringbuffer_struct,
                                uart1_read_buffer,
                                data_size);
        uart1_read_buffer[data_size] = '\0';
        uart_printf(&huart1, "data6:%s\r\n", uart1_read_buffer);
        memset(uart1_read_buffer, 0, data_size + 1U);
    }
}

int uart_printf(UART_HandleTypeDef *huart, const char *format, ...)
{
    char buffer[256];
    va_list args;
    int length;

    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length < 0)
    {
        return -1;
    }
    if ((uint32_t)length >= sizeof(buffer))
    {
        length = (int)sizeof(buffer) - 1;
    }
    return (HAL_UART_Transmit(huart, (uint8_t *)buffer, (uint16_t)length, 100U) ==
            HAL_OK) ? length : -1;
}
