#include "myusart.h"

extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;
uint8_t uart1_rx_dmabuffer[128];	//空闲中端数据缓存
uint8_t uart1_read_buffer[128];

struct rt_ringbuffer uart1_ringbuffer_struct;	//串口环形缓存区结构体用于管理我的环形缓存区
rt_uint8_t uart1_ringbuffer[128];	//环形缓存区实际大小

extern UART_HandleTypeDef huart2;
extern DMA_HandleTypeDef hdma_usart2_rx;
uint8_t uart2_rx_dmabuffer[1024];	//空闲中端数据缓存
uint8_t uart2_read_buffer[1024];

struct rt_ringbuffer uart2_ringbuffer_struct;	//串口环形缓存区结构体用于管理我的环形缓存区
rt_uint8_t uart2_ringbuffer[1024];	//环形缓存区实际大小
void myusart_init(void)
{
		rt_ringbuffer_init(&uart2_ringbuffer_struct,uart2_ringbuffer,sizeof(uart2_ringbuffer));//初始化环形缓存区
    rt_ringbuffer_init(&uart1_ringbuffer_struct,uart1_ringbuffer,sizeof(uart1_ringbuffer));//初始化环形缓存区
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1)
    {
        HAL_UART_DMAStop(huart);	//暂停DMA传输，保证我的DMA传输的完整性(因为我的DMA可能任然是活跃的没有搬运到128字节，它还在检测数据)

				if(rt_ringbuffer_space_len(&uart1_ringbuffer_struct)!=0)	//判断缓存区空间
				{
					uint16_t putsize=rt_ringbuffer_put(&uart1_ringbuffer_struct,uart1_rx_dmabuffer,Size);
					if(putsize!=Size)	//环形缓存区数据未全部放入
						uart_printf(&huart1,"Ringbuffer Size too Small\r\n");
				}
					
        HAL_UARTEx_ReceiveToIdle_DMA(huart, uart1_rx_dmabuffer, sizeof(uart1_rx_dmabuffer));	//打开DMA运输
			__HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);	//关闭DMA半中断
    }else if (huart->Instance==USART2)
    {
        HAL_UART_DMAStop(huart);	//暂停DMA传输，保证我的DMA传输的完整性(因为我的DMA可能任然是活跃的没有搬运到128字节，它还在检测数据)

				if(rt_ringbuffer_space_len(&uart2_ringbuffer_struct)!=0)	//判断缓存区空间
				{
					uint16_t putsize=rt_ringbuffer_put(&uart2_ringbuffer_struct,uart2_rx_dmabuffer,Size);
					if(putsize!=Size)	//环形缓存区数据未全部放入
						uart_printf(&huart2,"Ringbuffer Size too Small\r\n");
				}
					
        HAL_UARTEx_ReceiveToIdle_DMA(huart, uart2_rx_dmabuffer, sizeof(uart2_rx_dmabuffer));	//打开DMA运输
			__HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);	//关闭DMA半中断        
    }
}

void uart1_task(void)
{
	uint16_t data_size=rt_ringbuffer_data_len(&uart1_ringbuffer_struct);	//获取缓存区数据大小
	if(data_size>0)
	{
		rt_ringbuffer_get(&uart1_ringbuffer_struct,uart1_read_buffer,data_size);
		uart_printf(&huart1,"bootloader:%s\r\n",uart1_read_buffer);	//打印接收到的数据
		rt_ringbuffer_reset(&uart1_ringbuffer_struct);
    memset(uart1_read_buffer,0,data_size);  
		
	}
}

void uart2_task(void)
{
	uint16_t data_size=rt_ringbuffer_data_len(&uart2_ringbuffer_struct);	//获取缓存区数据大小
	if(data_size>0)
	{
		rt_ringbuffer_get(&uart2_ringbuffer_struct,uart2_read_buffer,data_size);
		uart_printf(&huart2,"%s\r\n",uart2_read_buffer);	//打印接收到的数据
		rt_ringbuffer_reset(&uart2_ringbuffer_struct);
    memset(uart1_read_buffer,0,data_size);  		
	}
}

//串口发送
int uart_printf(UART_HandleTypeDef* huart, const char* format, ...) {
    char buffer[256]; // 设定一个足够大的缓冲区
    va_list args;
    va_start(args, format);
    
    // 使用vsprintf进行格式化输出
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len < 0) {
        return -1;  // 格式化失败
    }
    
    // 通过UART发送格式化后的字符串
    HAL_UART_Transmit(huart, (uint8_t*)buffer, len, 10);
    return len;
}

