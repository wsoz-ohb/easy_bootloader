#ifndef EASY_BOOTLOADER_APP_H
#define EASY_BOOTLOADER_APP_H

#include "boot_config_app.h"

typedef enum {
    BOOT_PORT_APP_OK = 0,
    BOOT_PORT_APP_ERROR = -1,
    BOOT_PORT_APP_TIMEOUT = -2,
} boot_port_app_status_t;

/* 操作集 ops */
typedef struct {
    uint32_t (*get_tick)(void);
    boot_port_app_status_t (*boot_port_app_flash_erase)(uint32_t addr, uint32_t size);
    boot_port_app_status_t (*boot_port_app_flash_write)(uint32_t addr, const uint8_t *data, uint32_t len);
    boot_port_app_status_t (*boot_port_app_flash_read)(uint32_t addr, uint8_t *data, uint32_t len);
    boot_port_app_status_t (*boot_port_app_data_write)(const uint8_t *data, uint32_t len);
    uint32_t (*boot_port_app_data_read)(uint8_t *buf, uint32_t max_len);
    void (*boot_port_app_log)(const char *fmt, ...);
    void (*boot_port_app_system_reset)(void);
} boot_app_ops_t;

boot_port_app_status_t easy_bootloader_app_init(const boot_app_ops_t *ops);
void easy_bootloader_app_run(void);

#endif // !EASY_BOOTLOADER_APP_H
