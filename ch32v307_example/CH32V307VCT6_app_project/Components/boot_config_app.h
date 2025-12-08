#ifndef BOOT_CONFIG_APP_H
#define BOOT_CONFIG_APP_H

#include <stdint.h>

#define BOOT_APP_CONFIG_ENABLE_LOG        1U      // 1启用日志输出 0禁用日志输出

/*
 * Flash 布局（使用 CH32 别名地址 0x00000000 -> 0x08000000）
 */
#define BOOT_APP_FLAG_REGION_ADDR         0x0003F800U       // 别名地址
#define BOOT_APP_FLAG_REGION_SIZE         0x00000800U       // 2KB

/*
 * 标志位区布局
 */
#define BOOT_APP_FLAG_OFFSET              0x00U
#define BOOT_APP_VERSION_OFFSET           0x04U
#define BOOT_APP_DATE_OFFSET              0x08U

#define BOOT_APP_FLAG_ADDR                (BOOT_APP_FLAG_REGION_ADDR + BOOT_APP_FLAG_OFFSET)
#define BOOT_APP_VERSION_ADDR             (BOOT_APP_FLAG_REGION_ADDR + BOOT_APP_VERSION_OFFSET)
#define BOOT_APP_DATE_ADDR                (BOOT_APP_FLAG_REGION_ADDR + BOOT_APP_DATE_OFFSET)

/*
 * 协议缓冲配置
 */
#define BOOT_APP_PACKET_MAX_SIZE          1013U
#define BOOT_APP_RINGBUFFER_SIZE          1013U
#define BOOT_APP_UART_TIMEOUT_MS          5000U

#endif // BOOT_CONFIG_APP_H
