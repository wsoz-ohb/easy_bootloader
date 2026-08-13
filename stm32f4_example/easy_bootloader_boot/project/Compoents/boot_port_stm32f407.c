#include "boot_config.h"
#include "easy_bootloader.h"

#include "main.h"
#include "W25QXX_driver.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart5;

typedef struct
{
    uint32_t start_address;
    uint32_t size;
    uint32_t sector;
} stm32_flash_sector_t;

static const stm32_flash_sector_t stm32_flash_sectors[] =
{
    {0x08000000UL, 0x00004000UL, FLASH_SECTOR_0},
    {0x08004000UL, 0x00004000UL, FLASH_SECTOR_1},
    {0x08008000UL, 0x00004000UL, FLASH_SECTOR_2},
    {0x0800C000UL, 0x00004000UL, FLASH_SECTOR_3},
    {0x08010000UL, 0x00010000UL, FLASH_SECTOR_4},
    {0x08020000UL, 0x00020000UL, FLASH_SECTOR_5},
    {0x08040000UL, 0x00020000UL, FLASH_SECTOR_6},
    {0x08060000UL, 0x00020000UL, FLASH_SECTOR_7},
    {0x08080000UL, 0x00020000UL, FLASH_SECTOR_8},
    {0x080A0000UL, 0x00020000UL, FLASH_SECTOR_9},
    {0x080C0000UL, 0x00020000UL, FLASH_SECTOR_10},
    {0x080E0000UL, 0x00020000UL, FLASH_SECTOR_11},
};

#define STM32_FLASH_SECTOR_COUNT \
    (sizeof(stm32_flash_sectors) / sizeof(stm32_flash_sectors[0]))

static uint8_t external_flash_ready;

static void service_watchdog(void *context)
{
    (void)context;
    /* 示例未启用 IWDG，产品可在此喂狗。 */
}

static bool is_range_valid(uint32_t address,
                           uint32_t length,
                           uint32_t region_start,
                           uint32_t region_size)
{
    return (address >= region_start) &&
           (length <= region_size) &&
           ((address - region_start) <= (region_size - length));
}

static int sector_index_from_address(uint32_t address)
{
    uint32_t index;

    for (index = 0U; index < STM32_FLASH_SECTOR_COUNT; index++)
    {
        if ((address >= stm32_flash_sectors[index].start_address) &&
            (address < (stm32_flash_sectors[index].start_address +
                        stm32_flash_sectors[index].size)))
        {
            return (int)index;
        }
    }
    return -1;
}

static boot_loader_status_t erase_internal(uint32_t address, uint32_t length)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error;
    uint32_t index;
    int first;
    int last;

    if ((length == 0U) || ((address + length) < address))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    first = sector_index_from_address(address);
    last = sector_index_from_address(address + length - 1U);
    if ((first < 0) || (last < first))
    {
        return BOOT_LOADER_IO_ERROR;
    }

    /* STM32F407 按扇区擦除，请求范围覆盖到的扇区都会被擦除。 */
    HAL_FLASH_Unlock();
    for (index = (uint32_t)first; index <= (uint32_t)last; index++)
    {
        memset(&erase, 0, sizeof(erase));
        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        erase.Sector = stm32_flash_sectors[index].sector;
        erase.NbSectors = 1U;
        if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return BOOT_LOADER_IO_ERROR;
        }
        service_watchdog(NULL);
    }
    HAL_FLASH_Lock();
    return BOOT_LOADER_OK;
}

static boot_loader_status_t program_internal(uint32_t address,
                                              const uint8_t *data,
                                              uint32_t length)
{
    uint32_t offset;

    if ((data == NULL) || ((address & 3U) != 0U) || ((length & 3U) != 0U))
    {
        return BOOT_LOADER_IO_ERROR;
    }

    HAL_FLASH_Unlock();
    for (offset = 0U; offset < length; offset += 4U)
    {
        uint32_t word = (uint32_t)data[offset] |
                        ((uint32_t)data[offset + 1U] << 8) |
                        ((uint32_t)data[offset + 2U] << 16) |
                        ((uint32_t)data[offset + 3U] << 24);
        if ((HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + offset, word) != HAL_OK) ||
            (*(const volatile uint32_t *)(address + offset) != word))
        {
            HAL_FLASH_Lock();
            return BOOT_LOADER_IO_ERROR;
        }
    }
    HAL_FLASH_Lock();
    return BOOT_LOADER_OK;
}

static boot_loader_status_t bcb_read(void *context,
                                     uint32_t offset,
                                     uint8_t *data,
                                     uint32_t length)
{
    (void)context;
    if ((data == NULL) || !is_range_valid(offset,
                                          length,
                                          0U,
                                          BOOT_BCB_REGION_SIZE))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    memcpy(data, (const void *)(BOOT_BCB_REGION_ADDR + offset), length);
    return BOOT_LOADER_OK;
}

static boot_loader_status_t bcb_program(void *context,
                                        uint32_t offset,
                                        const uint8_t *data,
                                        uint32_t length)
{
    (void)context;
    if (!is_range_valid(offset,
                        length,
                        0U,
                        BOOT_BCB_REGION_SIZE))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    return program_internal(BOOT_BCB_REGION_ADDR + offset, data, length);
}

static boot_loader_status_t bcb_erase(void *context,
                                      uint32_t offset,
                                      uint32_t length)
{
    (void)context;
    if (!is_range_valid(offset,
                        length,
                        0U,
                        BOOT_BCB_REGION_SIZE))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    return erase_internal(BOOT_BCB_REGION_ADDR + offset, length);
}

static uint32_t slot_base_address(boot_slot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return BOOT_EXTERNAL_SLOT_A_OFFSET;
    }
    if (slot == BOOT_SLOT_B)
    {
        return BOOT_EXTERNAL_SLOT_B_OFFSET;
    }
    return 0U;
}

static uint32_t slot_capacity(boot_slot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return BOOT_EXTERNAL_SLOT_A_SIZE;
    }
    if (slot == BOOT_SLOT_B)
    {
        return BOOT_EXTERNAL_SLOT_B_SIZE;
    }
    return 0U;
}

static boot_loader_status_t external_read(void *context,
                                          boot_slot_t slot,
                                          uint32_t offset,
                                          uint8_t *data,
                                          uint32_t length)
{
    uint32_t address;
    uint32_t capacity;

    (void)context;
    capacity = slot_capacity(slot);
    address = slot_base_address(slot);
    if ((external_flash_ready == 0U) || (data == NULL) || (capacity == 0U) ||
        (length > 0xFFFFU) ||
        !is_range_valid(offset, length, 0U, capacity))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    return (BSP_W25Qxx_BufferRead(data, address + offset, (uint16_t)length) == W25Qx_OK) ?
           BOOT_LOADER_OK : BOOT_LOADER_IO_ERROR;
}

static boot_loader_status_t external_erase(void *context,
                                           boot_slot_t slot,
                                           uint32_t offset,
                                           uint32_t length)
{
    uint32_t address;
    uint32_t capacity;
    uint32_t current;
    uint32_t verify_offset;
    uint32_t index;
    uint8_t verify_buffer[BOOT_TRANSFER_BUFFER_SIZE];

    (void)context;
    capacity = slot_capacity(slot);
    address = slot_base_address(slot);
    if ((external_flash_ready == 0U) || (capacity == 0U) ||
        ((offset % 4096U) != 0U) ||
        ((length % 4096U) != 0U) ||
        !is_range_valid(offset, length, 0U, capacity))
    {
        return BOOT_LOADER_IO_ERROR;
    }

    for (current = 0U; current < length; current += 4096U)
    {
        if (BSP_W25Qxx_SectorErase(address + offset + current) != W25Qx_OK)
        {
            return BOOT_LOADER_IO_ERROR;
        }
        service_watchdog(NULL);
        for (verify_offset = 0U; verify_offset < 4096U;
             verify_offset += sizeof(verify_buffer))
        {
            if (BSP_W25Qxx_BufferRead(verify_buffer,
                                       address + offset + current + verify_offset,
                                       sizeof(verify_buffer)) != W25Qx_OK)
            {
                return BOOT_LOADER_IO_ERROR;
            }
            for (index = 0U; index < sizeof(verify_buffer); index++)
            {
                if (verify_buffer[index] != 0xFFU)
                {
                    return BOOT_LOADER_IO_ERROR;
                }
            }
        }
        service_watchdog(NULL);
    }
    return BOOT_LOADER_OK;
}

static boot_loader_status_t external_write(void *context,
                                           boot_slot_t slot,
                                           uint32_t offset,
                                           const uint8_t *data,
                                           uint32_t length)
{
    static uint8_t verify_buffer[BOOT_TRANSFER_BUFFER_SIZE];
    uint32_t address;
    uint32_t capacity;
    uint32_t current;

    (void)context;
    capacity = slot_capacity(slot);
    address = slot_base_address(slot);
    if ((external_flash_ready == 0U) || (data == NULL) || (capacity == 0U) ||
        !is_range_valid(offset, length, 0U, capacity))
    {
        return BOOT_LOADER_IO_ERROR;
    }

    for (current = 0U; current < length;)
    {
        uint32_t chunk = length - current;
        if (chunk > sizeof(verify_buffer))
        {
            chunk = sizeof(verify_buffer);
        }
        if (BSP_W25Qxx_BufferWrite((uint8_t *)&data[current],
                                   address + offset + current,
                                   (uint16_t)chunk) != W25Qx_OK)
        {
            return BOOT_LOADER_IO_ERROR;
        }
        if ((BSP_W25Qxx_BufferRead(verify_buffer,
                                   address + offset + current,
                                   (uint16_t)chunk) != W25Qx_OK) ||
            (memcmp(verify_buffer, &data[current], chunk) != 0))
        {
            return BOOT_LOADER_IO_ERROR;
        }
        current += chunk;
    }
    return BOOT_LOADER_OK;
}

static boot_loader_status_t app_erase(void *context,
                                      uint32_t offset,
                                      uint32_t length)
{
    (void)context;
    if (!is_range_valid(offset, length, 0U, BOOT_APP_MAX_SIZE))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    return erase_internal(BOOT_APP_START_ADDR + offset, length);
}

static boot_loader_status_t app_write(void *context,
                                      uint32_t offset,
                                      const uint8_t *data,
                                      uint32_t length)
{
    (void)context;
    if (!is_range_valid(offset, length, 0U, BOOT_APP_MAX_SIZE))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    return program_internal(BOOT_APP_START_ADDR + offset, data, length);
}

static boot_loader_status_t app_read(void *context,
                                     uint32_t offset,
                                     uint8_t *data,
                                     uint32_t length)
{
    (void)context;
    if ((data == NULL) || !is_range_valid(offset, length, 0U, BOOT_APP_MAX_SIZE))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    memcpy(data, (const void *)(BOOT_APP_START_ADDR + offset), length);
    return BOOT_LOADER_OK;
}

static void jump_to_app(void *context, uint32_t app_address)
{
    typedef void (*app_entry_t)(void);
    uint32_t stack_pointer;
    uint32_t reset_handler;
    uint32_t index;

    (void)context;
    stack_pointer = *(const volatile uint32_t *)app_address;
    reset_handler = *(const volatile uint32_t *)(app_address + 4U);

    /* 清理 Bootloader 的中断和外设状态后再切换向量表。 */
    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    HAL_UART_DMAStop(&huart1);
    HAL_UART_DMAStop(&huart5);
    HAL_UART_DeInit(&huart1);
    HAL_UART_DeInit(&huart5);
    HAL_DeInit();

    for (index = 0U; index < 8U; index++)
    {
        NVIC->ICER[index] = 0xFFFFFFFFUL;
        NVIC->ICPR[index] = 0xFFFFFFFFUL;
    }

    SCB->VTOR = app_address;
    __set_MSP(stack_pointer);
    __set_PSP(0U);
    __set_CONTROL(0U);
    __DSB();
    __ISB();
    /* NVIC 已全部关闭，可恢复复位态 PRIMASK 后进入 APP。 */
    __set_PRIMASK(0U);
    ((app_entry_t)reset_handler)();

    for (;;)
    {
    }
}

static void boot_log(void *context, const char *format, ...)
{
    char buffer[160];
    va_list args;
    int length;

    (void)context;
    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length > 0)
    {
        uint32_t transmit_length = (uint32_t)length;
        if (transmit_length >= sizeof(buffer))
        {
            transmit_length = sizeof(buffer) - 1U;
        }
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)buffer, transmit_length, 100U);
    }
}

static const boot_loader_ops_t boot_port_ops =
{
    NULL,
    bcb_read,
    bcb_program,
    bcb_erase,
    external_read,
    external_erase,
    external_write,
    app_erase,
    app_write,
    app_read,
    service_watchdog,
    jump_to_app,
    boot_log,
};

void bootloader_app_init(void)
{
    boot_loader_config_t config;
    uint32_t jedec_id;

    easy_bootloader_get_default_config(&config);
    jedec_id = BSP_W25Qxx_Read_ID();
    if (jedec_id != 0xEF4018UL)
    {
        boot_log(NULL, "W25Q128 unavailable: 0x%06lX; internal App only\r\n",
                 (unsigned long)jedec_id);
    }
    else
    {
        external_flash_ready = 1U;
    }
    (void)easy_bootloader_init(&config, &boot_port_ops);
}

void bootloader_app_loop(void)
{
    easy_bootloader_run();
}
