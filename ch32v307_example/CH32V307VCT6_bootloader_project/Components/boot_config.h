//配置头文件
#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <stdint.h>

#define BOOT_CONFIG_ENABLE_LOG        1U      // 1启用日志输出 0禁用日志输出

/*
 * CPU 架构选择
 */
#define BOOT_ARCH_ARM_CORTEX_M        1U
#define BOOT_ARCH_RISCV               2U

#define BOOT_ARCH                     BOOT_ARCH_RISCV  // 当前架构

/*
 * Flash 布局（CH32V 采用 0x00000000 别名地址映射到物理 0x08000000）
 */
#define BOOT_BOOTLOADER_START_ADDR    0x00000000U       // 别名地址
#define BOOT_BOOTLOADER_SIZE          0x00006000U       // 24KB

#define BOOT_APP_START_ADDR           0x00006000U       // 别名地址（物理 0x08006000）
#define BOOT_APP_MAX_SIZE             0x00039800U       // 230KB
#define BOOT_APP_END_ADDR             (BOOT_APP_START_ADDR + BOOT_APP_MAX_SIZE - 1U)

#define BOOT_FLAG_REGION_ADDR         0x0003F800U       // 别名地址（物理 0x0803F800）
#define BOOT_FLAG_REGION_SIZE         0x00000800U       // 2KB

/*
 * 标志位区布局
 */
#define BOOT_FLAG_OFFSET              0x00U
#define BOOT_VERSION_OFFSET           0x04U
#define BOOT_DATE_OFFSET              0x08U

#define BOOT_FLAG_ADDR                (BOOT_FLAG_REGION_ADDR + BOOT_FLAG_OFFSET)
#define BOOT_VERSION_ADDR             (BOOT_FLAG_REGION_ADDR + BOOT_VERSION_OFFSET)
#define BOOT_DATE_ADDR                (BOOT_FLAG_REGION_ADDR + BOOT_DATE_OFFSET)

/* 标志位值定义 */
#define BOOT_FLAG_BOOTLOADER          1U
#define BOOT_FLAG_APP                 2U
#define BOOT_FLAG_ERASED              0xE339E339U      // CH32 Flash 擦除后的默认值

/*
 * SRAM 范围（用于校验 APP 栈指针有效性）
 */
#define BOOT_SRAM_START_ADDR          0x20000000U
#define BOOT_SRAM_END_ADDR            0x2000FFFFU       // 64KB RAM

/*
 * CH32V 无 CCM RAM
 */
#define BOOT_HAS_CCM                  0U

/*
 * 协议缓冲配置
 */
#define BOOT_PACKET_MAX_SIZE          1024U
#define BOOTLOADER_RINGBUFFER_SIZE    1024U
#define BOOT_UART_TIMEOUT_MS          5000U

#endif // BOOT_CONFIG_H
