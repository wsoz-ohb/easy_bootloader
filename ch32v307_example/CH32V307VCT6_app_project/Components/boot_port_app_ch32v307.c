#include "boot_config_app.h"
#include "easy_bootloader_app.h"
#include "bsp_sys.h"

#define FLASH_BASE_ADDR       0x08000000U
#define FLASH_PHYS_ADDR(addr) ((uint32_t)((addr) + FLASH_BASE_ADDR))

uint32_t boot_port_app_get_tick(void)
{
    return get_uwtick();
}

boot_port_app_status_t boot_port_app_flash_erase(uint32_t addr, uint32_t size)
{
    if ((addr % 256U) || (size % 256U))
        return BOOT_PORT_APP_ERROR;

    uint32_t phys_addr = FLASH_PHYS_ADDR(addr);

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR | FLASH_FLAG_BSY);

    for (uint32_t addr_now = phys_addr; addr_now < phys_addr + size; addr_now += 256U)
    {
        FLASH_Status flash_status = FLASH_ErasePage(addr_now);
        if (flash_status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return BOOT_PORT_APP_ERROR;
        }
    }

    FLASH_Lock();
    return BOOT_PORT_APP_OK;
}

boot_port_app_status_t boot_port_app_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if ((addr % 4U) || (len % 4U) || data == NULL)
        return BOOT_PORT_APP_ERROR;

    uint32_t phys_addr = FLASH_PHYS_ADDR(addr);

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR | FLASH_FLAG_BSY);

    for (uint32_t addr_now = phys_addr; addr_now < phys_addr + len; addr_now += 4U, data += 4U)
    {
        uint32_t word = ((uint32_t)data[3] << 24) |
                        ((uint32_t)data[2] << 16) |
                        ((uint32_t)data[1] <<  8) |
                        ((uint32_t)data[0]      );

        FLASH_Status flash_status = FLASH_ProgramWord(addr_now, word);
        if (flash_status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return BOOT_PORT_APP_ERROR;
        }
    }

    FLASH_Lock();
    return BOOT_PORT_APP_OK;
}

boot_port_app_status_t boot_port_app_flash_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    uint32_t phys_addr = FLASH_PHYS_ADDR(addr);
    memcpy(data, (const void *)phys_addr, len);
    return BOOT_PORT_APP_OK;
}

boot_port_app_status_t boot_port_app_data_write(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0U) {
        return BOOT_PORT_APP_ERROR;
    }

    for (uint32_t i = 0; i < len; i++) {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) {
        }
        USART_SendData(USART2, data[i]);
    }

    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET) {
    }

    return BOOT_PORT_APP_OK;
}

uint32_t boot_port_app_data_read(uint8_t *buf, uint32_t max_len)
{
    if (buf == NULL || max_len == 0U) {
        return 0U;
    }

    rt_uint16_t data_len = rt_ringbuffer_data_len(&uart2_ringbuffer);
    if (data_len == 0U) {
        return 0U;
    }

    rt_uint16_t request = (data_len > max_len) ? (rt_uint16_t)max_len : data_len;
    return rt_ringbuffer_get(&uart2_ringbuffer, buf, request);
}

void boot_port_app_log(const char *fmt, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (len > 0)
    {
        uint32_t tx_len = (uint32_t)len;
        if (tx_len >= sizeof(buffer)) {
            tx_len = sizeof(buffer) - 1U;
        }
        for (uint32_t i = 0; i < tx_len; i++) {
            while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {
            }
            USART_SendData(USART1, buffer[i]);
        }
        while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) {
        }
    }
}

void boot_port_app_system_reset(void)
{
    NVIC_SystemReset();
}

boot_app_ops_t boot_port_app_ops = {
    .get_tick = boot_port_app_get_tick,
    .boot_port_app_flash_erase = boot_port_app_flash_erase,
    .boot_port_app_flash_write = boot_port_app_flash_write,
    .boot_port_app_flash_read = boot_port_app_flash_read,
    .boot_port_app_data_write = boot_port_app_data_write,
    .boot_port_app_data_read = boot_port_app_data_read,
    .boot_port_app_log = boot_port_app_log,
    .boot_port_app_system_reset = boot_port_app_system_reset,
};

void bootloader_app_init(void)
{
    (void)easy_bootloader_app_init(&boot_port_app_ops);
}

void bootloader_app_loop(void)
{
    easy_bootloader_app_run();
}
