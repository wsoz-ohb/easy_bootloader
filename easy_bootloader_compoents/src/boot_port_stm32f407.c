#include "boot_port.h"
#include "boot_config.h"
#include "main.h"
#include "ringbuffer.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* 外部变量声明 */
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern DMA_HandleTypeDef hdma_usart2_rx;
extern struct rt_ringbuffer uart2_ringbuffer_struct;

/* STM32F407 Flash 扇区信息 */
typedef struct {
    uint32_t start_addr;
    uint32_t size;
    uint32_t sector_id;
} flash_sector_t;

static const flash_sector_t flash_sectors[] = {
    {0x08000000, 0x4000,  FLASH_SECTOR_0},   // 16KB
    {0x08004000, 0x4000,  FLASH_SECTOR_1},   // 16KB
    {0x08008000, 0x4000,  FLASH_SECTOR_2},   // 16KB
    {0x0800C000, 0x4000,  FLASH_SECTOR_3},   // 16KB
    {0x08010000, 0x10000, FLASH_SECTOR_4},   // 64KB  - APP 起始
    {0x08020000, 0x20000, FLASH_SECTOR_5},   // 128KB
    {0x08040000, 0x20000, FLASH_SECTOR_6},   // 128KB
    {0x08060000, 0x20000, FLASH_SECTOR_7},   // 128KB
    {0x08080000, 0x20000, FLASH_SECTOR_8},   // 128KB
    {0x080A0000, 0x20000, FLASH_SECTOR_9},   // 128KB
    {0x080C0000, 0x20000, FLASH_SECTOR_10},  // 128KB
    {0x080E0000, 0x20000, FLASH_SECTOR_11},  // 128KB - 标志区
};
#define FLASH_SECTOR_COUNT (sizeof(flash_sectors) / sizeof(flash_sectors[0]))


uint32_t boot_port_get_tick(void)
{
    return HAL_GetTick();
}

/* 根据地址获取扇区索引 */
static int get_sector_index(uint32_t addr)
{
    for (int i = 0; i < FLASH_SECTOR_COUNT; i++) {
        if (addr >= flash_sectors[i].start_addr &&
            addr < flash_sectors[i].start_addr + flash_sectors[i].size) {
            return i;
        }
    }
    return -1;
}

boot_port_status_t boot_port_flash_erase(uint32_t addr, uint32_t size)
{
    uint32_t end_addr = addr + size;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0;
    HAL_StatusTypeDef status;

    /* 获取起始和结束扇区 */
    int start_sector = get_sector_index(addr);
    int end_sector = get_sector_index(end_addr - 1);

    if (start_sector < 0 || end_sector < 0) {
        return BOOT_PORT_ERROR;
    }

    /* 解锁 Flash */
    HAL_FLASH_Unlock();

    /* 逐个扇区擦除 */
    for (int i = start_sector; i <= end_sector; i++) {
        erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;  // 2.7V - 3.6V
        erase_init.Sector = flash_sectors[i].sector_id;
        erase_init.NbSectors = 1;

        status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
        if (status != HAL_OK) {
            HAL_FLASH_Lock();
            return BOOT_PORT_ERROR;
        }
    }

    HAL_FLASH_Lock();
    return BOOT_PORT_OK;
}

boot_port_status_t boot_port_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    HAL_StatusTypeDef status;
    uint32_t i;

    /* 解锁 Flash */
    HAL_FLASH_Unlock();

    /* 以 WORD (32位) 为单位写入 */
    for (i = 0; i < len; i += 4) {
        uint32_t word = *(uint32_t *)(data + i);
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word);
        if (status != HAL_OK) {
            HAL_FLASH_Lock();
            return BOOT_PORT_ERROR;
        }
    }

    HAL_FLASH_Lock();
    return BOOT_PORT_OK;
}

boot_port_status_t boot_port_flash_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    memcpy(data, (const void *)addr, len);
    return BOOT_PORT_OK;
}

boot_port_status_t boot_port_uart_write(const uint8_t *data, uint32_t len)
{
    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart2, (uint8_t *)data, len, 1000);
    return (status == HAL_OK) ? BOOT_PORT_OK : BOOT_PORT_ERROR;
}

uint32_t boot_port_uart_read(uint8_t *buf, uint32_t max_len)
{
    return rt_ringbuffer_get(&uart2_ringbuffer_struct, buf, max_len);
}

void boot_port_log(const char *fmt, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (len > 0) {
        HAL_UART_Transmit(&huart1, (uint8_t *)buffer, len, 100);
    }
}

void boot_port_jump_to_app(uint32_t app_addr)
{
    typedef void (*pFunction)(void);
    pFunction jump_to_app;

    uint32_t app_stack = *(volatile uint32_t *)app_addr;
    uint32_t app_reset = *(volatile uint32_t *)(app_addr + 4);

    /* 1. 关闭全局中断 */
    __disable_irq();

    /* 2. 关闭 SysTick */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    /* 3. 复位外设 - 停止 DMA 和 UART */
    HAL_UART_DMAStop(&huart1);
    HAL_UART_DMAStop(&huart2);
    HAL_UART_DeInit(&huart1);
    HAL_UART_DeInit(&huart2);

    /* 4. 清除所有中断挂起标志 */
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;  // 禁用所有中断
        NVIC->ICPR[i] = 0xFFFFFFFF;  // 清除所有挂起中断
    }

    /* 5. 设置向量表偏移 */
    SCB->VTOR = app_addr;

    /* 6. 设置主栈指针 */
    __set_MSP(app_stack);
    __set_PSP(0U);  // 确保线程栈指针与新 APP 状态一致

    /* 7. 内存屏障并重新允许中断 */
    __DSB();
    __ISB();
    __set_PRIMASK(0U);   // 重新开启全局中断
    __enable_irq();      // 双保险，确保 Cortex-M 允许中断

    /* 8. 跳转到 APP 复位向量 */
    jump_to_app = (pFunction)app_reset;
    jump_to_app();

    /* 不应该执行到这里 */
    while (1);
}

void boot_port_system_reset(void)
{
    NVIC_SystemReset(); // 调用系统复位函数
}

