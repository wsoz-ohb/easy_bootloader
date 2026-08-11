//应用层头文件
#ifndef EASY_BOOTLOADER_H
#define EASY_BOOTLOADER_H

#include "boot_config.h"
#include <stdbool.h>

/* 协议固定字段长度: 2B头 + 3B剩余 + 2B长度 + 2B校验 + 2B尾 */
#define BOOT_FRAME_FIXED_SIZE     11U
/* 纯数据部分最大长度 = 整帧最大长度 - 固定部分长度 */
#define BOOT_PAYLOAD_MAX_SIZE     (BOOT_PACKET_MAX_SIZE - BOOT_FRAME_FIXED_SIZE)

/* Bootloader 状态枚举 */
typedef enum {
    BOOT_STATE_IDLE = 0,
    BOOT_STATE_RECEIVING,
    BOOT_STATE_WAIT_FINISH,
} boot_state_t;

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

typedef struct {
    uint8_t  rx_cache[BOOT_PACKET_MAX_SIZE];   // 线性解析缓存（整帧最大长度）
    uint16_t rx_cache_len;
    uint8_t  payload_buf[BOOT_PAYLOAD_MAX_SIZE];  // 纯数据缓存

    uint32_t current_addr;              //当前写入地址
    uint8_t  stream_cache[4];           //写流缓存，保证4字节对齐写入
    uint8_t  stream_cache_len;

    uint32_t boot_flag;
    uint32_t app_version;
    uint32_t update_date;
    uint32_t out_flash_flag;

    boot_state_t state;                 // 当前状态
    bool download_active;
    bool initialized;
} bootloader_context_t;

extern bootloader_context_t g_boot_ctx;

boot_port_status_t easy_bootloader_init(const boot_ops_t *ops);
void easy_bootloader_run(void);

#endif // EASY_BOOTLOADER_H
