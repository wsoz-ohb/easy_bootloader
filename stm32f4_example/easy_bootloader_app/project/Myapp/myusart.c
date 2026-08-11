#include "myusart.h"
#include "W25QXX_driver.h"
#include <stdbool.h>

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

#define OTA_DATA_START_ADDR   (0x001000U)

#define BOOT_FLAG_BOOTLOADER  (1U)
#define BOOT_FLAG_APP         (2U)

static const uint8_t g_boot_ack[] = {0x55U, 0xAAU, 0xFFU, 0xFEU, 0x55U, 0x55U};
static const uint8_t g_start_flash_cmd[] = {0x55U, 0xAAU, 0xFFU, 0xEEU, 0x55U, 0x55U};
extern boot_port_app_status_t boot_port_app_flash_erase(uint32_t addr, uint32_t size);
extern boot_port_app_status_t boot_port_app_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);
extern boot_port_app_status_t boot_port_app_flash_read(uint32_t addr, uint8_t *data, uint32_t len);
extern void boot_port_app_system_reset(void);

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
		uart_printf(&huart1,"data2:%s\r\n",uart1_read_buffer);	//打印接收到的数据
		rt_ringbuffer_reset(&uart1_ringbuffer_struct);
    memset(uart1_read_buffer,0,data_size);  
		
	}
}

//判断完成帧
bool is_frame_complete(const uint8_t* buffer, size_t length)
{
    const size_t finish_len = 14U;

    if (buffer == NULL || length < finish_len)
    {
        return false;
    }

    for (size_t i = 0; (i + finish_len) <= length; i++)
    {
        if (buffer[i + 0] == 0x55U &&
            buffer[i + 1] == 0xAAU &&
            buffer[i + 10] == 0xFFU &&
            buffer[i + 11] == 0xFDU &&
            buffer[i + 12] == 0x55U &&
            buffer[i + 13] == 0x55U)
        {
            return true;
        }
    }

    return false;
}

static bool find_start_flash_cmd(const uint8_t *buffer, size_t length, size_t *pos)
{
    size_t cmd_len = sizeof(g_start_flash_cmd);

    if (buffer == NULL || length < cmd_len)
    {
        return false;
    }

    for (size_t i = 0; (i + cmd_len) <= length; i++)
    {
        if (memcmp(&buffer[i], g_start_flash_cmd, cmd_len) == 0)
        {
            if (pos != NULL)
            {
                *pos = i;
            }
            return true;
        }
    }

    return false;
}

void ota_flash_update_metadata(void)
{
    uint32_t end_addr = ota_flash_get_write_addr();

    if (end_addr <= OTA_DATA_START_ADDR)
    {
        uart_printf(&huart1, "[OTA] data empty\r\n");
        return;
    }

    uart_printf(&huart1,
                "[OTA] data ready: addr=0x%06lX, len=%lu\r\n",
                (unsigned long)OTA_DATA_START_ADDR,
                (unsigned long)(end_addr - OTA_DATA_START_ADDR));
}

static bool ota_write_boot_region_flags(uint32_t boot_flag, uint32_t out_flash_flag)
{
    uint32_t words[4] = {0};

    if (boot_port_app_flash_read(BOOT_APP_VERSION_ADDR, (uint8_t *)&words[1], 4U) != BOOT_PORT_APP_OK)
    {
        words[1] = 0xFFFFFFFFU;
    }

    if (boot_port_app_flash_read(BOOT_APP_DATE_ADDR, (uint8_t *)&words[2], 4U) != BOOT_PORT_APP_OK)
    {
        words[2] = 0xFFFFFFFFU;
    }

    words[0] = boot_flag;
    words[3] = out_flash_flag;

    if (boot_port_app_flash_erase(BOOT_APP_FLAG_REGION_ADDR, BOOT_APP_FLAG_REGION_SIZE) != BOOT_PORT_APP_OK)
    {
        uart_printf(&huart1, "[OTA] erase boot flag region failed\r\n");
        return false;
    }

    if (boot_port_app_flash_write(BOOT_APP_FLAG_ADDR, (const uint8_t *)words, sizeof(words)) != BOOT_PORT_APP_OK)
    {
        uart_printf(&huart1, "[OTA] write boot flag region failed\r\n");
        return false;
    }

    return true;
}

static bool ota_set_out_flash_empty_before_erase(void)
{
    if (!ota_write_boot_region_flags(BOOT_FLAG_APP, OUT_FLASH_FLAG_EMPTY))
    {
        return false;
    }

    uart_printf(&huart1, "[OTA] OUT_FLASH_FLAG=EMPTY\r\n");
    return true;
}

static void ota_set_boot_ready_and_reset(void)
{
    if (!ota_write_boot_region_flags(BOOT_FLAG_BOOTLOADER, OUT_FLASH_FLAG_READY))
    {
        return;
    }

    uart_printf(&huart1, "[OTA] BOOT_FLAG=BOOTLOADER, OUT_FLASH_FLAG=READY\r\n");
    uart_printf(&huart1, "[OTA] set boot flag done, reset now\r\n");
    for (volatile uint32_t i = 0; i < 100000U; i++);
    boot_port_app_system_reset();
}

void uart2_task(void)
{
	uint16_t data_size=rt_ringbuffer_data_len(&uart2_ringbuffer_struct);	//获取缓存区数据大小
	static uint32_t next=OTA_DATA_START_ADDR;
	static bool ota_rx_started = false;
	if(data_size>0)
	{
		rt_ringbuffer_get(&uart2_ringbuffer_struct,uart2_read_buffer,data_size);

		const uint8_t *payload = uart2_read_buffer;
		uint16_t payload_len = data_size;

		if (!ota_rx_started)
		{
			size_t cmd_pos = 0U;
			if (!find_start_flash_cmd(uart2_read_buffer, data_size, &cmd_pos))
			{
				uart_printf(&huart1, "[OTA] waiting start cmd\r\n");
				rt_ringbuffer_reset(&uart2_ringbuffer_struct);
				memset(uart2_read_buffer,0,data_size);
				return;
			}

			ota_rx_started = true;
			next = OTA_DATA_START_ADDR;
			ota_flash_set_write_addr(next);

			if (!ota_set_out_flash_empty_before_erase())
			{
				uart_printf(&huart1, "[OTA] set OUT_FLASH_FLAG=EMPTY failed\r\n");
				ota_rx_started = false;
				rt_ringbuffer_reset(&uart2_ringbuffer_struct);
				memset(uart2_read_buffer,0,data_size);
				return;
			}

			/* 改为在 ota_flash_write_buffer() 内按需扇区擦除，避免启动时长时间等待 */
			uart_printf(&huart1, "[OTA] erase mode: sector-on-demand\r\n");

			HAL_UART_Transmit(&huart2, (uint8_t*)g_boot_ack, sizeof(g_boot_ack), 10);
			uart_printf(&huart1, "[OTA] start cmd detected, enter recv mode\r\n");

			cmd_pos += sizeof(g_start_flash_cmd);
			if (cmd_pos >= data_size)
			{
				rt_ringbuffer_reset(&uart2_ringbuffer_struct);
				memset(uart2_read_buffer,0,data_size);
				return;
			}

			payload = &uart2_read_buffer[cmd_pos];
			payload_len = (uint16_t)(data_size - (uint16_t)cmd_pos);
		}

		ota_flash_set_write_addr(next);
		uint32_t written = ota_flash_write_buffer(payload, payload_len);
		if(written==payload_len)
		{
			//判断完成帧
			if(is_frame_complete(payload,payload_len))
			{
				uart_printf(&huart1,"Frame Complete\r\n");
				//修改外部FLash	metadata
				ota_flash_update_metadata();
				HAL_UART_Transmit(&huart2, (uint8_t*)g_boot_ack, sizeof(g_boot_ack), 10);
				ota_rx_started = false;
				next = OTA_DATA_START_ADDR;
				ota_set_boot_ready_and_reset();

				return;
			}
			next = ota_flash_get_write_addr();	
			//接收到了，ACK下一包
			HAL_UART_Transmit(&huart2, (uint8_t*)g_boot_ack, sizeof(g_boot_ack), 10);	
			uart_printf(&huart1,"Write Flash Success\r\n");
		}else
		{
		    uart_printf(&huart1,"Write Flash Failed\r\n");
		}
		
		rt_ringbuffer_reset(&uart2_ringbuffer_struct);
    memset(uart2_read_buffer,0,data_size);  		
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
