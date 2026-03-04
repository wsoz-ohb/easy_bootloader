//应用层头文件
#ifndef EASY_BOOTLOADER_H
#define EASY_BOOTLOADER_H

#include "boot_config.h"


typedef enum {
    BOOT_PORT_OK = 0,
    BOOT_PORT_ERROR = -1,
    BOOT_PORT_TIMEOUT = -2,
} boot_port_status_t;

/*操作ops*/
typedef struct 
{
    uint32_t (*get_tick)(void);
    boot_port_status_t (*boot_port_flash_erase)(uint32_t addr, uint32_t size);
    boot_port_status_t (*boot_port_flash_write)(uint32_t addr, const uint8_t *data, uint32_t len);
    boot_port_status_t (*boot_port_flash_read)(uint32_t addr, uint8_t *data, uint32_t len);
    boot_port_status_t (*boot_port_data_write)(const uint8_t *data, uint32_t len);
    uint32_t (*boot_port_data_read)(uint8_t *buf, uint32_t max_len);
    void (*boot_port_log)(const char *fmt, ...);
    void (*boot_port_jump_to_app)(uint32_t app_addr);
    void (*boot_port_system_reset)(void);
}boot_ops_t;


boot_port_status_t easy_bootloader_init(const boot_ops_t *ops);
void easy_bootloader_run(void);

#endif // EASY_BOOTLOADER_H
