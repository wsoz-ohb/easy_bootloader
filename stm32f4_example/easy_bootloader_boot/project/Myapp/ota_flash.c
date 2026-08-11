#include "ota_flash.h"

#include "W25QXX_driver.h"
#include "myusart.h"

extern UART_HandleTypeDef huart1;

#define OTA_DATA_START_ADDR      (0x001000U)
#define OTA_STREAM_MAX_LEN       (8U * 1024U * 1024U)

static uint32_t g_read_offset;
static bool g_stream_valid;

void ota_flash_init(void)
{
    uint32_t jedec_id = BSP_W25Qxx_Read_ID();

    g_read_offset = 0U;
    g_stream_valid = true;

    uart_printf(&huart1, "[W25Q] JEDEC ID: 0x%06lX\r\n", (unsigned long)jedec_id);

    if (jedec_id == 0xEF4017UL)
    {
        uart_printf(&huart1, "[W25Q] detect W25Q64 ok\r\n");
    }
    else if (jedec_id == 0xEF4018UL)
    {
        uart_printf(&huart1, "[W25Q] detect W25Q128 ok\r\n");
    }
    else
    {
        uart_printf(&huart1, "[W25Q] warning: unexpected id, check wiring/CS/SPI\r\n");
    }

    uart_printf(&huart1,
                "[OTA] flash stream use OUT_FLASH_FLAG, start=0x%06lX\r\n",
                (unsigned long)OTA_DATA_START_ADDR);
}

bool ota_flash_stream_ready(void)
{
    return g_stream_valid && (g_read_offset < OTA_STREAM_MAX_LEN);
}

uint32_t ota_flash_read(uint8_t *buf, uint32_t max_len)
{
    if (buf == NULL || max_len == 0U || !ota_flash_stream_ready())
    {
        return 0U;
    }

    uint32_t remain = OTA_STREAM_MAX_LEN - g_read_offset;
    uint32_t chunk = (max_len < remain) ? max_len : remain;

    if (chunk > 0xFFFFU)
    {
        chunk = 0xFFFFU;
    }

    if (chunk == 0U)
    {
        return 0U;
    }

    if (BSP_W25Qxx_BufferRead(buf, OTA_DATA_START_ADDR + g_read_offset, (uint16_t)chunk) != W25Qx_OK)
    {
        uart_printf(&huart1,
                    "[OTA] flash read fail at 0x%06lX\r\n",
                    (unsigned long)(OTA_DATA_START_ADDR + g_read_offset));
        g_stream_valid = false;
        return 0U;
    }

    g_read_offset += chunk;
    return chunk;
}
