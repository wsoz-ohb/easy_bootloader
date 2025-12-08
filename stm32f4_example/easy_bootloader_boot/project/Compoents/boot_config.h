//配置头文件
#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <stdint.h>

#define BOOT_CONFIG_ENABLE_LOG        1U      // 1启用日志输出 0禁用日志输出

/*
 * CPU 架构选择
 * 移植时根据目标 MCU 修改
 */
#define BOOT_ARCH_ARM_CORTEX_M        1U      // ARM Cortex-M (STM32, GD32 等)
#define BOOT_ARCH_RISCV               2U      // RISC-V (CH32V 等)

#define BOOT_ARCH                     BOOT_ARCH_ARM_CORTEX_M  // 当前架构

/*
 * Flash 布局
 */
#define BOOT_BOOTLOADER_START_ADDR    0x08000000U
#define BOOT_BOOTLOADER_SIZE          0x00010000U

#define BOOT_APP_START_ADDR           0x08010000U
#define BOOT_APP_MAX_SIZE             0x000D0000U
#define BOOT_APP_END_ADDR             (BOOT_APP_START_ADDR + BOOT_APP_MAX_SIZE - 1U)

#define BOOT_FLAG_REGION_ADDR         0x080E0000U
#define BOOT_FLAG_REGION_SIZE         0x00020000U

/*
 * 标志位区布局 (基于 BOOT_FLAG_REGION_ADDR)
 * Word 0: bootloader_flag  - 启动标志 (1=Bootloader模式, 2=APP模式)
 * Word 1: app_version      - 应用版本号
 * Word 2: update_date      - 更新日期 (格式: 0xYYYYMMDD, 如 0x20251201)
 */
#define BOOT_FLAG_OFFSET              0x00U
#define BOOT_VERSION_OFFSET           0x04U
#define BOOT_DATE_OFFSET              0x08U

#define BOOT_FLAG_ADDR                (BOOT_FLAG_REGION_ADDR + BOOT_FLAG_OFFSET)
#define BOOT_VERSION_ADDR             (BOOT_FLAG_REGION_ADDR + BOOT_VERSION_OFFSET)
#define BOOT_DATE_ADDR                (BOOT_FLAG_REGION_ADDR + BOOT_DATE_OFFSET)

/* 标志位值定义 */
#define BOOT_FLAG_BOOTLOADER          1U      // 停留在 Bootloader 模式
#define BOOT_FLAG_APP                 2U      // 跳转到 APP 模式
#define BOOT_FLAG_ERASED              0xFFFFFFFFU  // 未初始化（Flash 擦除后的值）

/*
 * SRAM 范围（用于校验 APP 栈指针有效性）
 * 移植时根据目标 MCU 修改
 */
#define BOOT_SRAM_START_ADDR          0x20000000U
#define BOOT_SRAM_END_ADDR            0x20030000U

/*
 * CCM 配置（部分 MCU 如 STM32F4 有 CCM RAM）
 * 0=无CCM, 1=有CCM
 * STM32F1/GD32/CH32 等无 CCM 的芯片设为 0
 */
#define BOOT_HAS_CCM                  1U
#if BOOT_HAS_CCM
#define BOOT_CCM_START_ADDR           0x10000000U
#define BOOT_CCM_END_ADDR             0x10010000U
#endif

/*
 * 协议缓冲配置
 */
#define BOOT_PACKET_MAX_SIZE          1013U
#define BOOTLOADER_RINGBUFFER_SIZE    1013U
#define BOOT_UART_TIMEOUT_MS          5000U


#endif // BOOT_CONFIG_H
