/*
 * boot_port_ch32v307.c
 *
 *  Created on: 2025123
 *      Author: 26410
 */
#include "boot_port.h"
#include "boot_config.h"
#include "bsp_sys.h"

#define FLASH_HW_ADDR(addr)   ((uint32_t)((addr) - BOOT_BOOTLOADER_START_ADDR) + FLASH_BASE)


uint32_t boot_port_get_tick(void)
{
    return get_uwtick();
}


boot_port_status_t boot_port_flash_erase(uint32_t addr, uint32_t size)
{
    if ((addr % 256U) || (size % 256U))
        return BOOT_PORT_ERROR;

    uint32_t phys_start = FLASH_HW_ADDR(addr);
    uint32_t phys_end   = phys_start + size;
    const uint32_t page_size  = 256U;
    const uint32_t block_size = 32U * 1024U;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR | FLASH_FLAG_BSY);

    uint32_t addr_now = phys_start;

    /* 先用快擦页对齐到 32KB 边界 */
    while ((addr_now < phys_end) && ((addr_now & (block_size - 1U)) != 0U)) {
        FLASH_ErasePage_Fast(addr_now);
        addr_now += page_size;
    }

    /* 使用 32KB 快擦接口加速大范围擦除 */
    while ((phys_end - addr_now) >= block_size) {
        FLASH_EraseBlock_32K_Fast(addr_now);
        addr_now += block_size;
    }

    /* 剩余尾部不足 32KB 的部分使用快擦页清理 */
    while (addr_now < phys_end) {
        FLASH_ErasePage_Fast(addr_now);
        addr_now += page_size;
    }

    FLASH_Lock();
    return BOOT_PORT_OK;
}



boot_port_status_t boot_port_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if ((addr % 4U) || (len % 4U) || data == NULL)
        return BOOT_PORT_ERROR;

    uint32_t phys_addr = FLASH_HW_ADDR(addr);

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
            return BOOT_PORT_ERROR;
        }
    }

    FLASH_Lock();
    return BOOT_PORT_OK;
}



boot_port_status_t boot_port_flash_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    uint32_t phys_addr = FLASH_HW_ADDR(addr);
    memcpy(data, (const void *)phys_addr, len);
    return BOOT_PORT_OK;
}



boot_port_status_t boot_port_uart_write(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0U) {
        return BOOT_PORT_ERROR;
    }

    for (uint32_t i = 0; i < len; i++) {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) {
        }
        USART_SendData(USART2, data[i]);
    }

    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET) {
    }

    return BOOT_PORT_OK;
}


uint32_t boot_port_uart_read(uint8_t *buf, uint32_t max_len)
{
    if (buf == NULL || max_len == 0U) {
        return 0U;
    }

    rt_uint16_t data_len = rt_ringbuffer_data_len(&uart2_ringbuffer);
    if (data_len == 0U) {
        return 0U;
    }

    rt_uint16_t request = (data_len > max_len) ? (rt_uint16_t)max_len : data_len;
    rt_uint16_t read_len = rt_ringbuffer_get(&uart2_ringbuffer, buf, request);
    return read_len;
}


void boot_port_log(const char *fmt, ...)
{

    char buffer[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (len > 0)
    {
        uart_printf(USART1, "%s", buffer);
    }
}


/**
 * @brief  跳转到应用程序。
 * @param  app_addr 应用程序的起始地址 (通常是 Flash 中 Bootloader 之后的位置)。
 * @note   采用官方推荐方式：通过软件中断实现跳转
 */
void boot_port_jump_to_app(uint32_t app_addr)
{
    /* 关闭中断并停止 Boot 阶段外设，避免残余中断打到 APP */
    __disable_irq();

    TIM_ITConfig(TIM6, TIM_IT_Update, DISABLE);
    TIM_Cmd(TIM6, DISABLE);
    USART_ITConfig(USART2, USART_IT_RXNE, DISABLE);
    USART_Cmd(USART2, DISABLE);

    /* 停止 SysTick，避免跨区触发异常 */
    SysTick->CTLR = 0;
    SysTick->SR   = 0;

    /* 屏蔽并清除所有中断挂起 */
    for (int i = 0; i < 8; i++) {
        PFIC->IRER[i] = 0xFFFFFFFF;
        PFIC->IPRR[i] = 0xFFFFFFFF;
    }

    /* 关闭外设时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, DISABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, DISABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, DISABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, DISABLE);

    /* 时钟外设复位，确保干净环境 */
    RCC_DeInit();

    Delay_Ms(10);

    /* 通过软件中断跳转到 APP（官方推荐方式） */
    NVIC_DisableIRQ(Software_IRQn);
    NVIC_ClearPendingIRQ(Software_IRQn);
    NVIC_EnableIRQ(Software_IRQn);
    __enable_irq();
    NVIC_SetPendingIRQ(Software_IRQn);

    while (1) {
        /* 等待软件中断处理，理论不会返回 */
    }
}




void boot_port_system_reset(void)
{
    NVIC_SystemReset();
}
