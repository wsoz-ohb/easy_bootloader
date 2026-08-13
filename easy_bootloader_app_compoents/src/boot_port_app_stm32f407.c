#include "easy_bootloader_app.h"

#include "W25QXX_driver.h"
#include "main.h"
#include "ringbuffer.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define BOOT_APP_BCB_REGION_ADDR           0x0800C000UL
#define BOOT_APP_BCB_REGION_SIZE           (16UL * 1024UL)

#define BOOT_APP_EXTERNAL_SLOT_A_OFFSET    (2UL * 1024UL * 1024UL)
#define BOOT_APP_EXTERNAL_SLOT_A_SIZE      (2UL * 1024UL * 1024UL)
#define BOOT_APP_EXTERNAL_SLOT_B_OFFSET    (4UL * 1024UL * 1024UL)
#define BOOT_APP_EXTERNAL_SLOT_B_SIZE      (1UL * 1024UL * 1024UL)

#define BOOT_APP_STORAGE_IO_CHUNK          512U
#define BOOT_APP_W25Q128_JEDEC_ID          0xEF4018UL
#define BOOT_APP_CONFIRM_DELAY_MS          3000UL

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;
extern struct rt_ringbuffer uart3_ringbuffer_struct;

static uint8_t external_flash_ready;
static uint8_t confirmation_complete;
static uint32_t confirmation_due_ms;

static bool is_range_valid(uint32_t offset, uint32_t length, uint32_t capacity)
{
    return (offset <= capacity) && (length <= (capacity - offset));
}

static void make_empty_control(boot_control_status_t *status)
{
    memset(status, 0, sizeof(*status));
    status->state = BOOT_CONTROL_EMPTY;
    status->confirmed_slot = BOOT_SLOT_NONE;
    status->pending_slot = BOOT_SLOT_NONE;
}

static boot_app_status_t bcb_read(void *context,
                                  uint32_t offset,
                                  uint8_t *data,
                                  uint32_t length)
{
    (void)context;

    if ((data == NULL) ||
        !is_range_valid(offset, length, BOOT_APP_BCB_REGION_SIZE))
    {
        return BOOT_APP_IO_ERROR;
    }

    memcpy(data, (const void *)(BOOT_APP_BCB_REGION_ADDR + offset), length);
    return BOOT_APP_OK;
}

static boot_app_status_t bcb_program(void *context,
                                     uint32_t offset,
                                     const uint8_t *data,
                                     uint32_t length)
{
    uint32_t index;

    (void)context;
    if ((data == NULL) || ((length & 3U) != 0U) ||
        !is_range_valid(offset, length, BOOT_APP_BCB_REGION_SIZE))
    {
        return BOOT_APP_IO_ERROR;
    }

    HAL_FLASH_Unlock();
    for (index = 0U; index < length; index += 4U)
    {
        uint32_t word = (uint32_t)data[index] |
                        ((uint32_t)data[index + 1U] << 8) |
                        ((uint32_t)data[index + 2U] << 16) |
                        ((uint32_t)data[index + 3U] << 24);

        if ((HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                               BOOT_APP_BCB_REGION_ADDR + offset + index,
                               word) != HAL_OK) ||
            (*(const volatile uint32_t *)(BOOT_APP_BCB_REGION_ADDR +
                                          offset + index) != word))
        {
            HAL_FLASH_Lock();
            return BOOT_APP_IO_ERROR;
        }
    }
    HAL_FLASH_Lock();
    return BOOT_APP_OK;
}

static boot_app_status_t bcb_erase(void *context,
                                   uint32_t offset,
                                   uint32_t length)
{
    (void)context;
    (void)offset;
    (void)length;

    /* APP 不擦 BCB，整区回收只能由 Bootloader 执行。 */
    return BOOT_APP_IO_ERROR;
}

static int bcb_storage_read(void *context,
                            uint32_t offset,
                            uint8_t *data,
                            uint32_t length)
{
    return (int)bcb_read(context, offset, data, length);
}

static int bcb_storage_program(void *context,
                               uint32_t offset,
                               const uint8_t *data,
                               uint32_t length)
{
    return (int)bcb_program(context, offset, data, length);
}

static int bcb_storage_erase(void *context, uint32_t offset, uint32_t length)
{
    return (int)bcb_erase(context, offset, length);
}

static boot_control_storage_t bcb_storage(void)
{
    boot_control_storage_t storage;

    storage.context = NULL;
    storage.read = bcb_storage_read;
    storage.program = bcb_storage_program;
    storage.erase = bcb_storage_erase;
    storage.region_size = BOOT_APP_BCB_REGION_SIZE;
    return storage;
}

static uint32_t slot_base_address(boot_slot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return BOOT_APP_EXTERNAL_SLOT_A_OFFSET;
    }
    if (slot == BOOT_SLOT_B)
    {
        return BOOT_APP_EXTERNAL_SLOT_B_OFFSET;
    }
    return 0U;
}

static uint32_t slot_capacity(boot_slot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return BOOT_APP_EXTERNAL_SLOT_A_SIZE;
    }
    if (slot == BOOT_SLOT_B)
    {
        return BOOT_APP_EXTERNAL_SLOT_B_SIZE;
    }
    return 0U;
}

static boot_app_status_t storage_read(void *context,
                                      boot_slot_t slot,
                                      uint32_t offset,
                                      uint8_t *data,
                                      uint32_t length)
{
    uint32_t base;
    uint32_t capacity;
    uint32_t current;

    (void)context;
    base = slot_base_address(slot);
    capacity = slot_capacity(slot);
    if ((external_flash_ready == 0U) || (data == NULL) || (capacity == 0U) ||
        !is_range_valid(offset, length, capacity))
    {
        return BOOT_APP_IO_ERROR;
    }

    for (current = 0U; current < length;)
    {
        uint32_t chunk = length - current;

        if (chunk > BOOT_APP_STORAGE_IO_CHUNK)
        {
            chunk = BOOT_APP_STORAGE_IO_CHUNK;
        }
        if (BSP_W25Qxx_BufferRead(&data[current],
                                  base + offset + current,
                                  (uint16_t)chunk) != W25Qx_OK)
        {
            return BOOT_APP_IO_ERROR;
        }
        current += chunk;
    }
    return BOOT_APP_OK;
}

static boot_app_status_t storage_erase(void *context,
                                       boot_slot_t slot,
                                       uint32_t offset,
                                       uint32_t length)
{
    uint8_t verify[BOOT_APP_STORAGE_IO_CHUNK];
    uint32_t base;
    uint32_t capacity;
    uint32_t current;
    uint32_t verify_offset;
    uint32_t index;

    (void)context;
    base = slot_base_address(slot);
    capacity = slot_capacity(slot);
    if ((external_flash_ready == 0U) || (capacity == 0U) ||
        ((offset % BOOT_APP_DEFAULT_ERASE_SIZE) != 0U) ||
        ((length % BOOT_APP_DEFAULT_ERASE_SIZE) != 0U) ||
        !is_range_valid(offset, length, capacity))
    {
        return BOOT_APP_IO_ERROR;
    }

    for (current = 0U; current < length; current += BOOT_APP_DEFAULT_ERASE_SIZE)
    {
        if (BSP_W25Qxx_SectorErase(base + offset + current) != W25Qx_OK)
        {
            return BOOT_APP_IO_ERROR;
        }
        for (verify_offset = 0U;
             verify_offset < BOOT_APP_DEFAULT_ERASE_SIZE;
             verify_offset += sizeof(verify))
        {
            if (BSP_W25Qxx_BufferRead(verify,
                                      base + offset + current + verify_offset,
                                      sizeof(verify)) != W25Qx_OK)
            {
                return BOOT_APP_IO_ERROR;
            }
            for (index = 0U; index < sizeof(verify); index++)
            {
                if (verify[index] != 0xFFU)
                {
                    return BOOT_APP_IO_ERROR;
                }
            }
        }
    }
    return BOOT_APP_OK;
}

static boot_app_status_t storage_write(void *context,
                                       boot_slot_t slot,
                                       uint32_t offset,
                                       const uint8_t *data,
                                       uint32_t length)
{
    uint8_t verify[BOOT_APP_STORAGE_IO_CHUNK];
    uint32_t base;
    uint32_t capacity;
    uint32_t current;

    (void)context;
    base = slot_base_address(slot);
    capacity = slot_capacity(slot);
    if ((external_flash_ready == 0U) || (data == NULL) || (capacity == 0U) ||
        !is_range_valid(offset, length, capacity))
    {
        return BOOT_APP_IO_ERROR;
    }

    /* 写入后立即回读比较，成功后核心才会返回 ACK。 */
    for (current = 0U; current < length;)
    {
        uint32_t chunk = length - current;

        if (chunk > sizeof(verify))
        {
            chunk = sizeof(verify);
        }
        if ((BSP_W25Qxx_BufferWrite((uint8_t *)&data[current],
                                    base + offset + current,
                                    (uint16_t)chunk) != W25Qx_OK) ||
            (BSP_W25Qxx_BufferRead(verify,
                                   base + offset + current,
                                   (uint16_t)chunk) != W25Qx_OK) ||
            (memcmp(verify, &data[current], chunk) != 0))
        {
            return BOOT_APP_IO_ERROR;
        }
        current += chunk;
    }
    return BOOT_APP_OK;
}

static boot_app_status_t read_boot_control(void *context,
                                           boot_control_status_t *status)
{
    boot_control_storage_t storage;

    (void)context;
    if (status == NULL)
    {
        return BOOT_APP_INVALID_ARGUMENT;
    }

    storage = bcb_storage();
    if (!boot_control_load(&storage, status))
    {
        make_empty_control(status);
    }
    return BOOT_APP_OK;
}

static void load_running_image_metadata(boot_app_config_t *config)
{
    uint8_t raw_header[BOOT_IMAGE_HEADER_SIZE];
    boot_control_status_t control;
    boot_image_info_t image;
    boot_slot_t running_slot = BOOT_SLOT_NONE;

    if ((config == NULL) || (external_flash_ready == 0U) ||
        (read_boot_control(NULL, &control) != BOOT_APP_OK))
    {
        return;
    }

    if (control.state == BOOT_CONTROL_TRIAL)
    {
        running_slot = control.pending_slot;
    }
    else if (control.state == BOOT_CONTROL_CONFIRMED)
    {
        running_slot = control.confirmed_slot;
    }

    if (!boot_control_is_slot_valid(running_slot) ||
        (storage_read(NULL,
                      running_slot,
                      0U,
                      raw_header,
                      sizeof(raw_header)) != BOOT_APP_OK) ||
        !boot_image_header_decode(raw_header, &image) ||
        (image.target_address != config->target_address))
    {
        return;
    }

    config->running_version = image.firmware_version;
    config->running_build_date = image.build_date;
}

static boot_app_status_t mark_update_ready(void *context,
                                           boot_slot_t pending_slot,
                                           const boot_image_info_t *image)
{
    boot_control_status_t control;
    boot_control_storage_t storage;
    boot_app_status_t status;

    (void)context;
    if ((image == NULL) || !boot_control_is_slot_valid(pending_slot))
    {
        return BOOT_APP_INVALID_ARGUMENT;
    }

    status = read_boot_control(NULL, &control);
    if (status != BOOT_APP_OK)
    {
        return status;
    }
    if ((control.state != BOOT_CONTROL_EMPTY) &&
        (control.state != BOOT_CONTROL_CONFIRMED))
    {
        return BOOT_APP_BUSY;
    }

    storage = bcb_storage();
    if (boot_control_free_record_count(&storage) <
        BOOT_CONTROL_UPDATE_RECORD_RESERVE)
    {
        return BOOT_APP_BUSY;
    }

    control.state = BOOT_CONTROL_UPDATE_READY;
    control.pending_slot = pending_slot;
    control.pending_version = image->firmware_version;
    control.image_size = image->payload_size;
    control.image_crc32 = image->payload_crc32;
    control.boot_attempts = 0U;
    control.max_boot_attempts = 0U;
    control.last_error = 0U;

    return boot_control_append(&storage, &control) ? BOOT_APP_OK
                                                   : BOOT_APP_IO_ERROR;
}

static boot_app_status_t mark_confirmed(void *context,
                                        const boot_control_status_t *status)
{
    boot_control_storage_t storage;

    (void)context;
    if (status == NULL)
    {
        return BOOT_APP_INVALID_ARGUMENT;
    }

    storage = bcb_storage();
    if (boot_control_free_record_count(&storage) == 0U)
    {
        return BOOT_APP_BUSY;
    }
    return boot_control_append(&storage, status) ? BOOT_APP_OK
                                                  : BOOT_APP_IO_ERROR;
}

static uint32_t get_time_ms(void *context)
{
    (void)context;
    return HAL_GetTick();
}

static uint32_t transport_read(void *context, uint8_t *data, uint32_t capacity)
{
    (void)context;
    if ((data == NULL) || (capacity == 0U))
    {
        return 0U;
    }
    return rt_ringbuffer_get(&uart3_ringbuffer_struct, data, capacity);
}

static boot_app_status_t transport_write(void *context,
                                         const uint8_t *data,
                                         uint32_t length)
{
    (void)context;
    if ((data == NULL) || (length > 0xFFFFU))
    {
        return BOOT_APP_INVALID_ARGUMENT;
    }
    return (HAL_UART_Transmit(&huart3, (uint8_t *)data, (uint16_t)length, 1000U) ==
            HAL_OK) ? BOOT_APP_OK : BOOT_APP_IO_ERROR;
}

static void system_reset(void *context)
{
    (void)context;
    NVIC_SystemReset();
}

static void app_log(void *context, const char *format, ...)
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
        (void)HAL_UART_Transmit(&huart1,
                                (uint8_t *)buffer,
                                (uint16_t)transmit_length,
                                100U);
    }
}

static const boot_app_ops_t boot_port_ops =
{
    .context = NULL,
    .get_time_ms = get_time_ms,
    .transport_read = transport_read,
    .transport_write = transport_write,
    .read_boot_control = read_boot_control,
    .bcb_read = bcb_read,
    .bcb_program = bcb_program,
    .bcb_erase = bcb_erase,
    .storage_erase = storage_erase,
    .storage_write = storage_write,
    .storage_read = storage_read,
    .mark_update_ready = mark_update_ready,
    .mark_confirmed = mark_confirmed,
    .system_reset = system_reset,
    .log = app_log,
};

void bootloader_app_init(void)
{
    boot_app_config_t config;
    boot_app_status_t status;
    uint32_t jedec_id;

    easy_bootloader_app_get_default_config(&config);
    jedec_id = BSP_W25Qxx_Read_ID();
    external_flash_ready = (jedec_id == BOOT_APP_W25Q128_JEDEC_ID) ? 1U : 0U;
    load_running_image_metadata(&config);
    confirmation_complete = 0U;
    confirmation_due_ms = HAL_GetTick() + BOOT_APP_CONFIRM_DELAY_MS;

    if (external_flash_ready == 0U)
    {
        app_log(NULL, "W25Q128 unavailable: 0x%06lX\r\n",
                (unsigned long)jedec_id);
    }

    status = easy_bootloader_app_init(&config, &boot_port_ops);
    if (status != BOOT_APP_OK)
    {
        app_log(NULL, "bootloader app init failed: %d\r\n", (int)status);
    }
}

void bootloader_app_loop(void)
{
    boot_app_status_t status;

    easy_bootloader_app_run();

    /* 示例延时确认；产品应替换为真实健康检查。 */
    if ((confirmation_complete == 0U) &&
        ((int32_t)(HAL_GetTick() - confirmation_due_ms) >= 0))
    {
        status = easy_bootloader_app_confirm_running();
        if ((status == BOOT_APP_OK) || (status == BOOT_APP_BUSY))
        {
            confirmation_complete = 1U;
        }
        else
        {
            confirmation_due_ms = HAL_GetTick() + 1000U;
            app_log(NULL, "App confirmation failed: %d\r\n", (int)status);
        }
    }
}
