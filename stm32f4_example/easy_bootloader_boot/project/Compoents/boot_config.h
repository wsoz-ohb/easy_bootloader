#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <stdint.h>

#define BOOT_CONFIG_ENABLE_LOG          1U

/* 内部 Flash：Bootloader / BCB / APP。 */
#define BOOT_BOOTLOADER_START_ADDR      0x08000000UL
#define BOOT_BOOTLOADER_SIZE            (48UL * 1024UL)

#define BOOT_BCB_REGION_ADDR            0x0800C000UL
#define BOOT_BCB_REGION_SIZE            (16UL * 1024UL)

#define BOOT_APP_START_ADDR             0x08010000UL
#define BOOT_APP_MAX_SIZE               (960UL * 1024UL)
#define BOOT_APP_END_ADDR               (BOOT_APP_START_ADDR + BOOT_APP_MAX_SIZE - 1UL)

/* 外部 Flash：A/B 两槽均可容纳完整 APP。 */
#define BOOT_EXTERNAL_SLOT_A_OFFSET     (2UL * 1024UL * 1024UL)
#define BOOT_EXTERNAL_SLOT_A_SIZE       (2UL * 1024UL * 1024UL)
#define BOOT_EXTERNAL_SLOT_B_OFFSET     (4UL * 1024UL * 1024UL)
#define BOOT_EXTERNAL_SLOT_B_SIZE       (1UL * 1024UL * 1024UL)
#define BOOT_EXTERNAL_ERASE_SIZE        4096U

#define BOOT_TRANSFER_BUFFER_SIZE       512U
#define BOOT_DEFAULT_MAX_BOOT_ATTEMPTS  2U

/* APP 向量表允许的 SRAM 范围。 */
#define BOOT_SRAM_START_ADDR            0x20000000UL
#define BOOT_SRAM_END_ADDR              0x20030000UL
#define BOOT_HAS_CCM                    1U
#if BOOT_HAS_CCM
#define BOOT_CCM_START_ADDR             0x10000000UL
#define BOOT_CCM_END_ADDR               0x10010000UL
#endif

#endif /* BOOT_CONFIG_H */
