#include "easy_bootloader.h"

#include <stdbool.h>
#include <string.h>

typedef struct
{
    boot_loader_config_t config;
    const boot_loader_ops_t *ops;
    boot_control_status_t control;
    boot_loader_status_t last_status;
    uint8_t initialized;
    uint8_t processed;
    uint8_t transfer_buffer[BOOT_TRANSFER_BUFFER_SIZE + 4U];
} boot_loader_context_t;

static boot_loader_context_t boot;

static bool app_vector_is_valid(void);
static boot_loader_status_t crc_internal_app(uint32_t length,
                                             uint32_t expected_crc);
static boot_loader_status_t read_and_verify_slot(boot_slot_t slot,
                                                  boot_image_info_t *image);

static void service_watchdog(void)
{
    if ((boot.ops != NULL) && (boot.ops->service_watchdog != NULL))
    {
        boot.ops->service_watchdog(boot.ops->context);
    }
}

#if BOOT_CONFIG_ENABLE_LOG
#define BOOT_LOG(...)                                                        \
    do                                                                       \
    {                                                                        \
        if ((boot.ops != NULL) && (boot.ops->log != NULL))                  \
        {                                                                    \
            boot.ops->log(boot.ops->context, __VA_ARGS__);                  \
        }                                                                    \
    } while (0)
#else
#define BOOT_LOG(...) ((void)0)
#endif

static int bcb_read_adapter(void *context,
                            uint32_t offset,
                            uint8_t *data,
                            uint32_t length)
{
    (void)context;
    return (boot.ops->bcb_read(boot.ops->context, offset, data, length) ==
            BOOT_LOADER_OK) ? 0 : -1;
}

static int bcb_program_adapter(void *context,
                               uint32_t offset,
                               const uint8_t *data,
                               uint32_t length)
{
    (void)context;
    return (boot.ops->bcb_program(boot.ops->context, offset, data, length) ==
            BOOT_LOADER_OK) ? 0 : -1;
}

static int bcb_erase_adapter(void *context, uint32_t offset, uint32_t length)
{
    (void)context;
    return (boot.ops->bcb_erase(boot.ops->context, offset, length) ==
            BOOT_LOADER_OK) ? 0 : -1;
}

static boot_control_storage_t bcb_storage(void)
{
    boot_control_storage_t storage;

    storage.context = NULL;
    storage.read = bcb_read_adapter;
    storage.program = bcb_program_adapter;
    storage.erase = bcb_erase_adapter;
    storage.region_size = boot.config.bcb_region_size;
    return storage;
}

static uint32_t slot_size(boot_slot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return boot.config.slot_a_size;
    }
    if (slot == BOOT_SLOT_B)
    {
        return boot.config.slot_b_size;
    }
    return 0U;
}

static bool range_is_valid(uint32_t offset, uint32_t length, uint32_t capacity)
{
    return (offset <= capacity) && (length <= (capacity - offset));
}

static bool status_has_valid_confirmed_slot(const boot_control_status_t *status)
{
    return boot_control_is_slot_valid(status->confirmed_slot);
}

static bool append_control(const boot_control_status_t *status)
{
    boot_control_storage_t storage = bcb_storage();

    if (boot_control_append(&storage, status))
    {
        return boot_control_load(&storage, &boot.control);
    }

    /* 只有内部 APP 和外部确认镜像都有效时才允许回收 BCB。 */
    if ((status->state == BOOT_CONTROL_CONFIRMED) &&
        status_has_valid_confirmed_slot(status) &&
        (status->image_size != 0U) &&
        app_vector_is_valid() &&
        (crc_internal_app(status->image_size, status->image_crc32) ==
         BOOT_LOADER_OK) &&
        (read_and_verify_slot(status->confirmed_slot,
                               &(boot_image_info_t){0}) == BOOT_LOADER_OK) &&
        boot_control_recycle(&storage, status))
    {
        return boot_control_load(&storage, &boot.control);
    }
    return false;
}

static bool app_vector_is_valid(void)
{
    uint8_t vector[8];
    uint32_t stack;
    uint32_t reset;
    bool stack_valid;

    if (boot.ops->app_read(boot.ops->context, 0U, vector, sizeof(vector)) !=
        BOOT_LOADER_OK)
    {
        return false;
    }

    stack = (uint32_t)vector[0] |
            ((uint32_t)vector[1] << 8) |
            ((uint32_t)vector[2] << 16) |
            ((uint32_t)vector[3] << 24);
    reset = (uint32_t)vector[4] |
            ((uint32_t)vector[5] << 8) |
            ((uint32_t)vector[6] << 16) |
            ((uint32_t)vector[7] << 24);

    if ((stack == 0xFFFFFFFFUL) || (reset == 0xFFFFFFFFUL) ||
        ((reset & 1U) == 0U))
    {
        return false;
    }

    stack_valid = (stack >= BOOT_SRAM_START_ADDR) &&
                  (stack <= BOOT_SRAM_END_ADDR);
#if BOOT_HAS_CCM
    stack_valid = stack_valid || ((stack >= BOOT_CCM_START_ADDR) &&
                                  (stack <= BOOT_CCM_END_ADDR));
#endif
    return stack_valid &&
           ((reset & ~1UL) >= boot.config.app_start_address) &&
           ((reset & ~1UL) <=
            (boot.config.app_start_address + boot.config.app_max_size - 1U));
}

static boot_loader_status_t crc_external_slot(boot_slot_t slot,
                                              const boot_image_info_t *image)
{
    uint32_t offset;
    uint32_t crc_state = 0xFFFFFFFFUL;

    for (offset = 0U; offset < image->payload_size;)
    {
        uint32_t length = image->payload_size - offset;
        if (length > BOOT_TRANSFER_BUFFER_SIZE)
        {
            length = BOOT_TRANSFER_BUFFER_SIZE;
        }
        if (boot.ops->external_read(boot.ops->context,
                                    slot,
                                    image->payload_offset + offset,
                                    boot.transfer_buffer,
                                    length) != BOOT_LOADER_OK)
        {
            return BOOT_LOADER_IO_ERROR;
        }
        crc_state = boot_crc32_update(crc_state, boot.transfer_buffer, length);
        offset += length;
        service_watchdog();
    }
    return (boot_crc32_finish(crc_state) == image->payload_crc32) ?
           BOOT_LOADER_OK : BOOT_LOADER_VERIFY_ERROR;
}

static boot_loader_status_t read_and_verify_slot(boot_slot_t slot,
                                                 boot_image_info_t *image)
{
    uint8_t raw_header[BOOT_IMAGE_HEADER_SIZE];
    uint32_t size = slot_size(slot);
    boot_loader_status_t status;

    if (!boot_control_is_slot_valid(slot) ||
        (size <= BOOT_IMAGE_PAYLOAD_OFFSET) ||
        (boot.ops->external_read(boot.ops->context,
                                 slot,
                                 0U,
                                 raw_header,
                                 sizeof(raw_header)) != BOOT_LOADER_OK) ||
        !boot_image_header_decode(raw_header, image) ||
        (image->target_address != boot.config.app_start_address) ||
        (image->payload_size == 0U) ||
        (image->payload_size > boot.config.app_max_size) ||
        !range_is_valid(image->payload_offset, image->payload_size, size))
    {
        return BOOT_LOADER_IMAGE_INVALID;
    }

    status = crc_external_slot(slot, image);
    return status;
}

static boot_loader_status_t crc_internal_app(uint32_t length, uint32_t expected_crc)
{
    uint32_t offset;
    uint32_t crc_state = 0xFFFFFFFFUL;

    if ((length == 0U) || (length > boot.config.app_max_size))
    {
        return BOOT_LOADER_IMAGE_INVALID;
    }

    for (offset = 0U; offset < length;)
    {
        uint32_t chunk = length - offset;
        if (chunk > BOOT_TRANSFER_BUFFER_SIZE)
        {
            chunk = BOOT_TRANSFER_BUFFER_SIZE;
        }
        if (boot.ops->app_read(boot.ops->context,
                               offset,
                               boot.transfer_buffer,
                               chunk) != BOOT_LOADER_OK)
        {
            return BOOT_LOADER_IO_ERROR;
        }
        crc_state = boot_crc32_update(crc_state, boot.transfer_buffer, chunk);
        offset += chunk;
        service_watchdog();
    }
    return (boot_crc32_finish(crc_state) == expected_crc) ?
           BOOT_LOADER_OK : BOOT_LOADER_VERIFY_ERROR;
}

static boot_loader_status_t copy_slot_to_app(boot_slot_t slot,
                                             const boot_image_info_t *image)
{
    uint32_t offset;

    /* 擦内部 APP 前必须已经落盘 INSTALLING 状态。 */
    if (boot.ops->app_erase(boot.ops->context, 0U, boot.config.app_max_size) !=
        BOOT_LOADER_OK)
    {
        return BOOT_LOADER_IO_ERROR;
    }

    for (offset = 0U; offset < image->payload_size;)
    {
        uint32_t source_length = image->payload_size - offset;
        uint32_t program_length;

        if (source_length > BOOT_TRANSFER_BUFFER_SIZE)
        {
            source_length = BOOT_TRANSFER_BUFFER_SIZE;
        }
        if (boot.ops->external_read(boot.ops->context,
                                    slot,
                                    image->payload_offset + offset,
                                    boot.transfer_buffer,
                                    source_length) != BOOT_LOADER_OK)
        {
            return BOOT_LOADER_IO_ERROR;
        }

        program_length = (source_length + 3U) & ~3UL;
        if (program_length != source_length)
        {
            memset(&boot.transfer_buffer[source_length],
                   0xFF,
                   program_length - source_length);
        }
        if (boot.ops->app_write(boot.ops->context,
                                 offset,
                                 boot.transfer_buffer,
                                 program_length) != BOOT_LOADER_OK)
        {
            return BOOT_LOADER_IO_ERROR;
        }
        offset += source_length;
        service_watchdog();
    }

    return crc_internal_app(image->payload_size, image->payload_crc32);
}

static boot_loader_status_t write_external_header(boot_slot_t slot,
                                                  const boot_image_info_t *image)
{
    uint8_t raw_header[BOOT_IMAGE_HEADER_SIZE];

    boot_image_header_encode(image, raw_header);
    if (boot.ops->external_write(boot.ops->context,
                                 slot,
                                 0U,
                                 raw_header,
                                 BOOT_IMAGE_HEADER_SIZE - sizeof(uint32_t)) !=
        BOOT_LOADER_OK)
    {
        return BOOT_LOADER_IO_ERROR;
    }
    return boot.ops->external_write(boot.ops->context,
                                    slot,
                                    BOOT_IMAGE_HEADER_SIZE - sizeof(uint32_t),
                                    &raw_header[BOOT_IMAGE_HEADER_SIZE - sizeof(uint32_t)],
                                    sizeof(uint32_t));
}

static boot_loader_status_t back_up_internal_app(boot_slot_t slot,
                                                  boot_image_info_t *image)
{
    uint32_t offset;
    uint32_t erase_length;
    uint32_t crc_state = 0xFFFFFFFFUL;
    boot_loader_status_t status;

    /* 首次升级没有回滚槽，先备份整个内部 APP 区。 */
    erase_length = BOOT_IMAGE_PAYLOAD_OFFSET + boot.config.app_max_size;
    if ((erase_length % boot.config.external_erase_size) != 0U)
    {
        erase_length += boot.config.external_erase_size -
                        (erase_length % boot.config.external_erase_size);
    }
    if (!boot_control_is_slot_valid(slot) ||
        (slot_size(slot) < erase_length) ||
        !app_vector_is_valid())
    {
        return BOOT_LOADER_APP_INVALID;
    }
    if (boot.ops->external_erase(boot.ops->context,
                                 slot,
                                 0U,
                                 erase_length) !=
        BOOT_LOADER_OK)
    {
        return BOOT_LOADER_IO_ERROR;
    }
    service_watchdog();

    for (offset = 0U; offset < boot.config.app_max_size;)
    {
        uint32_t length = boot.config.app_max_size - offset;
        if (length > BOOT_TRANSFER_BUFFER_SIZE)
        {
            length = BOOT_TRANSFER_BUFFER_SIZE;
        }
        if (boot.ops->app_read(boot.ops->context,
                               offset,
                               boot.transfer_buffer,
                               length) != BOOT_LOADER_OK)
        {
            return BOOT_LOADER_IO_ERROR;
        }
        if (boot.ops->external_write(boot.ops->context,
                                     slot,
                                     BOOT_IMAGE_PAYLOAD_OFFSET + offset,
                                     boot.transfer_buffer,
                                     length) != BOOT_LOADER_OK)
        {
            return BOOT_LOADER_IO_ERROR;
        }
        crc_state = boot_crc32_update(crc_state, boot.transfer_buffer, length);
        offset += length;
        service_watchdog();
    }

    memset(image, 0, sizeof(*image));
    image->target_address = boot.config.app_start_address;
    image->firmware_version = boot.control.confirmed_version;
    image->payload_offset = BOOT_IMAGE_PAYLOAD_OFFSET;
    image->payload_size = boot.config.app_max_size;
    image->payload_crc32 = boot_crc32_finish(crc_state);
    status = write_external_header(slot, image);
    if (status != BOOT_LOADER_OK)
    {
        return status;
    }
    return read_and_verify_slot(slot, image);
}

static boot_loader_status_t restore_confirmed_image(void)
{
    boot_image_info_t image;
    boot_control_status_t confirmed;
    boot_loader_status_t status;

    if (!status_has_valid_confirmed_slot(&boot.control))
    {
        return BOOT_LOADER_ROLLBACK_ERROR;
    }
    status = read_and_verify_slot(boot.control.confirmed_slot, &image);
    if (status != BOOT_LOADER_OK)
    {
        return BOOT_LOADER_ROLLBACK_ERROR;
    }

    status = copy_slot_to_app(boot.control.confirmed_slot, &image);
    if (status != BOOT_LOADER_OK)
    {
        return BOOT_LOADER_ROLLBACK_ERROR;
    }
    if (!app_vector_is_valid())
    {
        return BOOT_LOADER_APP_INVALID;
    }

    confirmed = boot.control;
    confirmed.state = BOOT_CONTROL_CONFIRMED;
    confirmed.pending_slot = BOOT_SLOT_NONE;
    confirmed.pending_version = 0U;
    confirmed.image_size = image.payload_size;
    confirmed.image_crc32 = image.payload_crc32;
    confirmed.boot_attempts = 0U;
    confirmed.max_boot_attempts = 0U;
    confirmed.confirmed_version = image.firmware_version;
    confirmed.last_error = 0U;
    if (!append_control(&confirmed))
    {
        return BOOT_LOADER_BCB_ERROR;
    }

    BOOT_LOG("confirmed slot %u restored\r\n",
             (unsigned)confirmed.confirmed_slot);
    boot.ops->jump_to_app(boot.ops->context, boot.config.app_start_address);
    return BOOT_LOADER_OK;
}

static boot_loader_status_t enter_rollback(void)
{
    boot_control_status_t rollback = boot.control;

    if (!status_has_valid_confirmed_slot(&rollback))
    {
        return BOOT_LOADER_ROLLBACK_ERROR;
    }

    rollback.state = BOOT_CONTROL_ROLLBACK;
    rollback.last_error = 0U;
    if (!append_control(&rollback))
    {
        return BOOT_LOADER_BCB_ERROR;
    }
    return restore_confirmed_image();
}

static boot_loader_status_t start_trial(void)
{
    boot_control_status_t trial = boot.control;
    uint8_t limit = trial.max_boot_attempts;

    if (limit == 0U)
    {
        limit = boot.config.max_boot_attempts;
    }
    if ((limit == 0U) || (trial.boot_attempts >= limit))
    {
        return enter_rollback();
    }
    if (!app_vector_is_valid() ||
        (crc_internal_app(trial.image_size, trial.image_crc32) != BOOT_LOADER_OK))
    {
        return enter_rollback();
    }

    /* 跳转前记录尝试次数，未确认复位时才能触发回滚。 */
    trial.state = BOOT_CONTROL_TRIAL;
    trial.max_boot_attempts = limit;
    trial.boot_attempts++;
    if (!append_control(&trial))
    {
        return BOOT_LOADER_BCB_ERROR;
    }

    BOOT_LOG("trial boot %u/%u from slot %u\r\n",
             (unsigned)trial.boot_attempts,
             (unsigned)trial.max_boot_attempts,
             (unsigned)trial.pending_slot);
    boot.ops->jump_to_app(boot.ops->context, boot.config.app_start_address);
    return BOOT_LOADER_OK;
}

static boot_loader_status_t install_pending_image(void)
{
    boot_image_info_t image;
    boot_control_status_t installing;
    boot_control_status_t trial;
    boot_loader_status_t status;
    bool pending_is_backup;

    if (!boot_control_is_slot_valid(boot.control.pending_slot))
    {
        return BOOT_LOADER_IMAGE_INVALID;
    }
    pending_is_backup = (boot.control.state == BOOT_CONTROL_BACKUP_READY) &&
                        (boot.control.confirmed_slot == boot.control.pending_slot);
    status = read_and_verify_slot(boot.control.pending_slot, &image);
    if ((status != BOOT_LOADER_OK) ||
        ((boot.control.image_size != 0U) &&
         ((boot.control.image_size != image.payload_size) ||
          (boot.control.image_crc32 != image.payload_crc32))) ||
        ((boot.control.pending_version != 0U) &&
         (boot.control.pending_version != image.firmware_version)))
    {
        return (status == BOOT_LOADER_OK) ? BOOT_LOADER_IMAGE_INVALID : status;
    }

    /* 先提交 INSTALLING，安装中断电后可重新安装。 */
    installing = boot.control;
    installing.state = BOOT_CONTROL_INSTALLING;
    if (!append_control(&installing))
    {
        return BOOT_LOADER_BCB_ERROR;
    }

    status = copy_slot_to_app(installing.pending_slot, &image);
    if (status != BOOT_LOADER_OK)
    {
        return status;
    }
    if (!app_vector_is_valid())
    {
        return BOOT_LOADER_APP_INVALID;
    }

    if (pending_is_backup)
    {
        trial = boot.control;
        trial.state = BOOT_CONTROL_CONFIRMED;
        trial.pending_slot = BOOT_SLOT_NONE;
        trial.pending_version = 0U;
        trial.image_size = image.payload_size;
        trial.image_crc32 = image.payload_crc32;
        trial.boot_attempts = 0U;
        trial.max_boot_attempts = 0U;
        trial.confirmed_version = image.firmware_version;
        trial.last_error = 0U;
        if (!append_control(&trial))
        {
            return BOOT_LOADER_BCB_ERROR;
        }
        boot.ops->jump_to_app(boot.ops->context, boot.config.app_start_address);
        return BOOT_LOADER_OK;
    }

    /* 新 APP 确认前，不改变旧的 confirmed_slot。 */
    trial = boot.control;
    trial.state = BOOT_CONTROL_TRIAL;
    trial.image_size = image.payload_size;
    trial.image_crc32 = image.payload_crc32;
    trial.pending_version = image.firmware_version;
    trial.boot_attempts = 0U;
    trial.max_boot_attempts = boot.config.max_boot_attempts;
    trial.last_error = 0U;
    if (!append_control(&trial))
    {
        return BOOT_LOADER_BCB_ERROR;
    }
    return start_trial();
}

static boot_loader_status_t prepare_first_update_backup(void)
{
    boot_image_info_t backup;
    boot_control_status_t backed_up;
    boot_slot_t backup_slot;
    boot_loader_status_t status;

    if (status_has_valid_confirmed_slot(&boot.control))
    {
        return BOOT_LOADER_OK;
    }
    backup_slot = boot_control_other_slot(boot.control.pending_slot);
    status = back_up_internal_app(backup_slot, &backup);
    if (status != BOOT_LOADER_OK)
    {
        return status;
    }

    backed_up = boot.control;
    backed_up.state = BOOT_CONTROL_BACKUP_READY;
    backed_up.confirmed_slot = backup_slot;
    backed_up.confirmed_version = backup.firmware_version;
    backed_up.last_error = 0U;
    if (!append_control(&backed_up))
    {
        return BOOT_LOADER_BCB_ERROR;
    }
    BOOT_LOG("initial App backed up to slot %u\r\n", (unsigned)backup_slot);
    return BOOT_LOADER_OK;
}

/* UPDATE_READY 阶段还未擦除内部 APP，失败时可取消本次升级。 */
static boot_loader_status_t cancel_unstarted_update(void)
{
    boot_control_status_t confirmed = boot.control;
    boot_image_info_t image;

    if (!status_has_valid_confirmed_slot(&confirmed) ||
        !app_vector_is_valid() ||
        (read_and_verify_slot(confirmed.confirmed_slot, &image) !=
         BOOT_LOADER_OK) ||
        (crc_internal_app(image.payload_size, image.payload_crc32) !=
         BOOT_LOADER_OK))
    {
        return BOOT_LOADER_ROLLBACK_ERROR;
    }

    confirmed.state = BOOT_CONTROL_CONFIRMED;
    confirmed.pending_slot = BOOT_SLOT_NONE;
    confirmed.pending_version = 0U;
    confirmed.boot_attempts = 0U;
    confirmed.max_boot_attempts = 0U;
    confirmed.confirmed_version = image.firmware_version;
    confirmed.image_size = image.payload_size;
    confirmed.image_crc32 = image.payload_crc32;
    confirmed.last_error = 0U;
    if (!append_control(&confirmed))
    {
        return BOOT_LOADER_BCB_ERROR;
    }

    BOOT_LOG("pending update rejected; starting confirmed App\r\n");
    boot.ops->jump_to_app(boot.ops->context, boot.config.app_start_address);
    return BOOT_LOADER_OK;
}

/* BCB 回收中断电时，仅用与内部 APP CRC、长度一致的外部镜像重建状态。 */
static void recover_confirmed_control_after_bcb_loss(void)
{
    boot_slot_t slot;

    if (!app_vector_is_valid())
    {
        return;
    }

    for (slot = BOOT_SLOT_A; slot <= BOOT_SLOT_B; slot++)
    {
        boot_image_info_t image;
        boot_control_status_t confirmed;

        if ((read_and_verify_slot(slot, &image) != BOOT_LOADER_OK) ||
            (crc_internal_app(image.payload_size, image.payload_crc32) !=
             BOOT_LOADER_OK))
        {
            continue;
        }

        memset(&confirmed, 0, sizeof(confirmed));
        confirmed.state = BOOT_CONTROL_CONFIRMED;
        confirmed.confirmed_slot = slot;
        confirmed.pending_slot = BOOT_SLOT_NONE;
        confirmed.confirmed_version = image.firmware_version;
        confirmed.image_size = image.payload_size;
        confirmed.image_crc32 = image.payload_crc32;
        if (append_control(&confirmed))
        {
            BOOT_LOG("BCB recovered from confirmed slot %u\r\n",
                     (unsigned)slot);
        }
        return;
    }
}

static boot_loader_status_t process_boot(void)
{
    boot_control_storage_t storage = bcb_storage();
    boot_loader_status_t status;

    if (!boot_control_load(&storage, &boot.control))
    {
        memset(&boot.control, 0, sizeof(boot.control));
        boot.control.state = BOOT_CONTROL_EMPTY;
        recover_confirmed_control_after_bcb_loss();
    }

    switch (boot.control.state)
    {
        case BOOT_CONTROL_EMPTY:
            if (app_vector_is_valid())
            {
                BOOT_LOG("no BCB record; starting valid App\r\n");
                boot.ops->jump_to_app(boot.ops->context, boot.config.app_start_address);
                return BOOT_LOADER_OK;
            }
            return BOOT_LOADER_APP_INVALID;

        case BOOT_CONTROL_CONFIRMED:
            if (app_vector_is_valid() &&
                ((boot.control.image_size == 0U) ||
                 (crc_internal_app(boot.control.image_size,
                                   boot.control.image_crc32) == BOOT_LOADER_OK)))
            {
                BOOT_LOG("confirmed App start\r\n");
                boot.ops->jump_to_app(boot.ops->context, boot.config.app_start_address);
                return BOOT_LOADER_OK;
            }
            return enter_rollback();

        case BOOT_CONTROL_BACKUP_READY:
            status = install_pending_image();
            return (status == BOOT_LOADER_OK) ? status : enter_rollback();

        case BOOT_CONTROL_INSTALLING:
            status = install_pending_image();
            return (status == BOOT_LOADER_OK) ? status : enter_rollback();

        case BOOT_CONTROL_UPDATE_READY:
            status = prepare_first_update_backup();
            if (status != BOOT_LOADER_OK)
            {
                /* INSTALLING 前旧 APP 仍完整，首次备份失败可继续启动。 */
                if (!status_has_valid_confirmed_slot(&boot.control) &&
                    app_vector_is_valid())
                {
                    BOOT_LOG("initial backup failed; starting existing App\r\n");
                    boot.ops->jump_to_app(boot.ops->context,
                                          boot.config.app_start_address);
                    return BOOT_LOADER_OK;
                }
                return enter_rollback();
            }
            status = install_pending_image();
            if ((status != BOOT_LOADER_OK) &&
                (boot.control.state == BOOT_CONTROL_UPDATE_READY))
            {
                return cancel_unstarted_update();
            }
            return (status == BOOT_LOADER_OK) ? status : enter_rollback();

        case BOOT_CONTROL_TRIAL:
            return start_trial();

        case BOOT_CONTROL_ROLLBACK:
            return restore_confirmed_image();

        default:
            return BOOT_LOADER_BCB_ERROR;
    }
}

void easy_bootloader_get_default_config(boot_loader_config_t *config)
{
    if (config == NULL)
    {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->app_start_address = BOOT_APP_START_ADDR;
    config->app_max_size = BOOT_APP_MAX_SIZE;
    config->bcb_region_size = BOOT_BCB_REGION_SIZE;
    config->slot_a_size = BOOT_EXTERNAL_SLOT_A_SIZE;
    config->slot_b_size = BOOT_EXTERNAL_SLOT_B_SIZE;
    config->external_erase_size = BOOT_EXTERNAL_ERASE_SIZE;
    config->max_boot_attempts = BOOT_DEFAULT_MAX_BOOT_ATTEMPTS;
}

boot_loader_status_t easy_bootloader_init(const boot_loader_config_t *config,
                                          const boot_loader_ops_t *ops)
{
    if ((config == NULL) || (ops == NULL) ||
        (ops->bcb_read == NULL) || (ops->bcb_program == NULL) ||
        (ops->bcb_erase == NULL) || (ops->external_read == NULL) ||
        (ops->external_erase == NULL) || (ops->external_write == NULL) ||
        (ops->app_erase == NULL) || (ops->app_write == NULL) ||
        (ops->app_read == NULL) || (ops->jump_to_app == NULL) ||
        (config->app_max_size == 0U) ||
        (config->bcb_region_size < BOOT_CONTROL_RECORD_SIZE) ||
        ((config->bcb_region_size % BOOT_CONTROL_RECORD_SIZE) != 0U) ||
        (config->slot_a_size <= BOOT_IMAGE_PAYLOAD_OFFSET) ||
        (config->slot_b_size <= BOOT_IMAGE_PAYLOAD_OFFSET) ||
        (config->external_erase_size == 0U))
    {
        return BOOT_LOADER_INVALID_ARGUMENT;
    }

    memset(&boot, 0, sizeof(boot));
    boot.config = *config;
    boot.ops = ops;
    boot.last_status = BOOT_LOADER_OK;
    boot.initialized = 1U;
    easy_bootloader_run();
    return boot.last_status;
}

void easy_bootloader_run(void)
{
    if (!boot.initialized || boot.processed)
    {
        return;
    }
    boot.processed = 1U;
    boot.last_status = process_boot();
    if (boot.last_status != BOOT_LOADER_OK)
    {
        BOOT_LOG("bootloader stopped with status %d\r\n", (int)boot.last_status);
    }
}

void easy_bootloader_get_progress(boot_loader_progress_t *progress)
{
    if (progress == NULL)
    {
        return;
    }
    progress->control = boot.control;
    progress->last_status = boot.last_status;
    progress->initialized = boot.initialized;
    progress->processed = boot.processed;
}
