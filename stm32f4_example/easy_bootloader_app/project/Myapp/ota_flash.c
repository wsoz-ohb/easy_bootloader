#include "ota_flash.h"

#include "W25QXX_driver.h"
#include "myusart.h"

extern UART_HandleTypeDef huart1;

#define OTA_DATA_START_ADDR      (0x001000U)
#define OTA_STREAM_MAX_LEN       (8U * 1024U * 1024U)
#define OTA_ERASE_SECTOR_SIZE    (4U * 1024U)
#define OTA_ERASE_ALIGN_MASK     (OTA_ERASE_SECTOR_SIZE - 1U)

static uint32_t g_read_offset;
static bool g_stream_valid;
static uint32_t g_write_addr;
static uint32_t g_erased_end_addr;

static uint32_t ota_align_down(uint32_t addr)
{
    return addr & ~OTA_ERASE_ALIGN_MASK;
}

static void ota_flash_erase_to(uint32_t end_addr)
{
    uint32_t aligned_end = (end_addr + OTA_ERASE_ALIGN_MASK) & ~OTA_ERASE_ALIGN_MASK;

    if (g_erased_end_addr == 0U)
    {
        g_erased_end_addr = ota_align_down(g_write_addr);
    }

    while (g_erased_end_addr < aligned_end)
    {
        BSP_W25Qxx_SectorErase(g_erased_end_addr);
        g_erased_end_addr += OTA_ERASE_SECTOR_SIZE;
    }
}

void ota_flash_init(void)
{
    uint32_t jedec_id = BSP_W25Qxx_Read_ID();

    g_read_offset = 0U;
    g_stream_valid = true;
    g_write_addr = OTA_DATA_START_ADDR;
    g_erased_end_addr = ota_align_down(OTA_DATA_START_ADDR);

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

void ota_flash_set_write_addr(uint32_t addr)
{
    uint32_t new_addr = (addr < OTA_DATA_START_ADDR) ? OTA_DATA_START_ADDR : addr;

    if (new_addr <= OTA_DATA_START_ADDR || new_addr < g_write_addr)
    {
        g_erased_end_addr = ota_align_down(new_addr);
    }

    g_write_addr = new_addr;
}

uint32_t ota_flash_get_write_addr(void)
{
    return g_write_addr;
}

uint32_t ota_flash_write_buffer(const uint8_t *buffer, uint32_t len)
{
    if (buffer == NULL || len == 0U)
    {
        return 0U;
    }

    if (g_write_addr < OTA_DATA_START_ADDR)
    {
        g_write_addr = OTA_DATA_START_ADDR;
    }

    uint32_t used = g_write_addr - OTA_DATA_START_ADDR;
    if (used >= OTA_STREAM_MAX_LEN)
    {
        return 0U;
    }

    uint32_t remain = OTA_STREAM_MAX_LEN - used;
    if (len > remain)
    {
        len = remain;
    }

    if (len == 0U)
    {
        return 0U;
    }

    ota_flash_erase_to(g_write_addr + len);

    uint32_t written = 0U;

    while (written < len)
    {
        uint32_t chunk = len - written;
        if (chunk > 0xFFFFU)
        {
            chunk = 0xFFFFU;
        }

        BSP_W25Qxx_BufferWrite((uint8_t *)&buffer[written], g_write_addr, (uint16_t)chunk);

        g_write_addr += chunk;
        written += chunk;
    }

    return written;
}
