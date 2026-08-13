#ifndef EASY_BOOTLOADER_H
#define EASY_BOOTLOADER_H

#include "boot_config.h"
#include "boot_control.h"
#include "boot_image.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    BOOT_LOADER_OK = 0,
    BOOT_LOADER_INVALID_ARGUMENT = -1,
    BOOT_LOADER_BCB_ERROR = -2,
    BOOT_LOADER_IO_ERROR = -3,
    BOOT_LOADER_IMAGE_INVALID = -4,
    BOOT_LOADER_VERIFY_ERROR = -5,
    BOOT_LOADER_APP_INVALID = -6,
    BOOT_LOADER_ROLLBACK_ERROR = -7,
} boot_loader_status_t;

typedef struct
{
    void *context;

    /* BCB 接口使用相对 BCB 区域的偏移。 */
    boot_loader_status_t (*bcb_read)(void *context,
                                     uint32_t offset,
                                     uint8_t *data,
                                     uint32_t length);
    boot_loader_status_t (*bcb_program)(void *context,
                                        uint32_t offset,
                                        const uint8_t *data,
                                        uint32_t length);
    boot_loader_status_t (*bcb_erase)(void *context,
                                      uint32_t offset,
                                      uint32_t length);

    /* 外部存储接口使用相对所选槽的偏移。 */
    boot_loader_status_t (*external_read)(void *context,
                                          boot_slot_t slot,
                                          uint32_t offset,
                                          uint8_t *data,
                                          uint32_t length);
    boot_loader_status_t (*external_erase)(void *context,
                                           boot_slot_t slot,
                                           uint32_t offset,
                                           uint32_t length);
    boot_loader_status_t (*external_write)(void *context,
                                           boot_slot_t slot,
                                           uint32_t offset,
                                           const uint8_t *data,
                                           uint32_t length);

    /* APP 接口使用相对 APP 起始地址的偏移。 */
    boot_loader_status_t (*app_erase)(void *context,
                                      uint32_t offset,
                                      uint32_t length);
    boot_loader_status_t (*app_write)(void *context,
                                      uint32_t offset,
                                      const uint8_t *data,
                                      uint32_t length);
    boot_loader_status_t (*app_read)(void *context,
                                     uint32_t offset,
                                     uint8_t *data,
                                     uint32_t length);

    void (*service_watchdog)(void *context);
    void (*jump_to_app)(void *context, uint32_t app_address);
    void (*log)(void *context, const char *format, ...);
} boot_loader_ops_t;

typedef struct
{
    uint32_t app_start_address;
    uint32_t app_max_size;
    uint32_t bcb_region_size;
    uint32_t slot_a_size;
    uint32_t slot_b_size;
    uint32_t external_erase_size;
    uint8_t max_boot_attempts;
} boot_loader_config_t;

typedef struct
{
    boot_control_status_t control;
    boot_loader_status_t last_status;
    uint8_t initialized;
    uint8_t processed;
} boot_loader_progress_t;

void easy_bootloader_get_default_config(boot_loader_config_t *config);
boot_loader_status_t easy_bootloader_init(const boot_loader_config_t *config,
                                          const boot_loader_ops_t *ops);
void easy_bootloader_run(void);
void easy_bootloader_get_progress(boot_loader_progress_t *progress);

#ifdef __cplusplus
}
#endif

#endif /* EASY_BOOTLOADER_H */
