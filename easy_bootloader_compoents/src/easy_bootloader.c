// 应用层源文件
#include "easy_bootloader.h"

#include <stdbool.h>
#include <string.h>

/* 应用层日志封装，受 BOOT_CONFIG_ENABLE_LOG 宏控制 */
#if BOOT_CONFIG_ENABLE_LOG
    #define BOOT_LOG(fmt, ...)  boot_port_log(fmt, ##__VA_ARGS__)
#else
    #define BOOT_LOG(fmt, ...)  ((void)0)
#endif

#define BOOT_FRAME_HEADER0        0x55U
#define BOOT_FRAME_HEADER1        0xAAU
#define BOOT_FRAME_TAIL0          0x55U
#define BOOT_FRAME_TAIL1          0x55U
#define BOOT_FRAME_FIXED_SIZE     11U    // 2B 头 + 3B 剩余 + 2B 长度 + 2B 校验 + 2B 尾

static const uint8_t g_boot_ack[] = {0x55U, 0xAAU, 0xFFU, 0xFEU, 0x55U, 0x55U}; //ACK帧

// 纯数据部分最大长度 = 整帧最大长度 - 固定部分长度
#define BOOT_PAYLOAD_MAX_SIZE     (BOOT_PACKET_MAX_SIZE - BOOT_FRAME_FIXED_SIZE)

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

    bool download_active;
    bool initialized;
} bootloader_context_t;

static bootloader_context_t g_boot_ctx;

static void bootloader_reset_context(void);
static void bootloader_read_flag_region(void);
static bool bootloader_check_app_valid(void);
static void bootloader_poll_uart(void);
static void bootloader_consume_cache(uint16_t count);
static bool bootloader_try_extract_frame(uint32_t *remaining, uint16_t *payload_len);
static boot_port_status_t bootloader_handle_payload(uint32_t remaining, uint16_t payload_len);
static boot_port_status_t bootloader_prepare_download(void);
static boot_port_status_t bootloader_stream_write(const uint8_t *data, uint32_t len);
static boot_port_status_t bootloader_stream_flush(void);

void easy_bootloader_init(void)
{
    BOOT_LOG("=== Easy Bootloader Start ===\r\n");

    bootloader_reset_context();
    bootloader_read_flag_region();

    BOOT_LOG("Flag: 0x%08X, Version: 0x%08X, Date: 0x%08X\r\n",
              g_boot_ctx.boot_flag, g_boot_ctx.app_version, g_boot_ctx.update_date);

    // 判断是否需要跳转到 APP
    // 条件1: 标志位为 APP 模式
    // 条件2: APP 区域有效（非空且校验通过）
    bool should_jump = false;

    if (g_boot_ctx.boot_flag == BOOT_FLAG_BOOTLOADER) {
        // flag=1: 明确要求停留在 Bootloader，等待固件
        BOOT_LOG("Flag=BOOT, waiting for firmware...\r\n");
    } else {
        // flag=2 或 flag=BOOT_FLAG_ERASED 或其他值: 都尝试检查 APP 有效性
        BOOT_LOG("Checking APP validity...\r\n");
        if (bootloader_check_app_valid()) {
            if (g_boot_ctx.boot_flag == BOOT_FLAG_APP) {
                // flag=2 且 APP 有效: 正常跳转
                should_jump = true;
            } else if (g_boot_ctx.boot_flag == BOOT_FLAG_ERASED) {
                // flag=擦除值 且 APP 有效: 标志位未初始化，停留在 Bootloader 等待升级
                BOOT_LOG("Flag erased, staying in bootloader...\r\n");
                should_jump = false;
            } else {
                // 其他未知 flag 值，APP 有效但不跳转，等待用户处理
                BOOT_LOG("Flag=0x%08X (unknown), APP valid but not jumping\r\n",
                         g_boot_ctx.boot_flag);
            }
        } else {
            // APP 无效，无论 flag 是什么都不跳转
            BOOT_LOG("APP invalid, staying in bootloader\r\n");
        }
    }

    if (should_jump) {
        BOOT_LOG("APP valid, jumping to APP...\r\n");
        boot_port_jump_to_app(BOOT_APP_START_ADDR);
        // 如果跳转失败会返回这里
        BOOT_LOG("Jump failed, staying in bootloader\r\n");
    }

    g_boot_ctx.initialized = true;
    BOOT_LOG("Bootloader ready, waiting for data...\r\n");
}

static void bootloader_read_flag_region(void)
{
    boot_port_flash_read(BOOT_FLAG_ADDR, (uint8_t *)&g_boot_ctx.boot_flag, 4U);
    boot_port_flash_read(BOOT_VERSION_ADDR, (uint8_t *)&g_boot_ctx.app_version, 4U);
    boot_port_flash_read(BOOT_DATE_ADDR, (uint8_t *)&g_boot_ctx.update_date, 4U);
}

static bool bootloader_check_app_valid(void)
{
    uint32_t app_word0 = *(volatile uint32_t *)BOOT_APP_START_ADDR;
    uint32_t app_word1 = *(volatile uint32_t *)(BOOT_APP_START_ADDR + 4U);

#if (BOOT_ARCH == BOOT_ARCH_ARM_CORTEX_M)
    // ARM Cortex-M 架构: 向量表格式为 [栈指针, 复位向量, ...]
    uint32_t app_stack = app_word0;
    uint32_t app_reset = app_word1;

    BOOT_LOG("APP Stack: 0x%08X, Reset: 0x%08X\r\n", app_stack, app_reset);

    // 检查1: 栈指针必须在 SRAM 范围内（或 CCM，如果有的话）
    bool stack_valid = (app_stack >= BOOT_SRAM_START_ADDR) && (app_stack <= BOOT_SRAM_END_ADDR);
#if BOOT_HAS_CCM
    if (!stack_valid) {
        stack_valid = (app_stack >= BOOT_CCM_START_ADDR) && (app_stack <= BOOT_CCM_END_ADDR);
    }
#endif
    if (!stack_valid) {
        BOOT_LOG("Invalid stack pointer\r\n");
        return false;
    }

    // 检查2: 复位向量必须在 APP 区域内
    if (app_reset < BOOT_APP_START_ADDR || app_reset > BOOT_APP_END_ADDR) {
        BOOT_LOG("Invalid reset vector\r\n");
        return false;
    }

    // 检查3: 复位向量必须是奇数（Thumb 模式）
    if ((app_reset & 0x1U) == 0U) {
        BOOT_LOG("Reset vector not Thumb mode\r\n");
        return false;
    }

    // 检查4: 确保不是全 0xFF（未烧录）
    if (app_stack == BOOT_FLAG_ERASED || app_reset == BOOT_FLAG_ERASED) {
        BOOT_LOG("APP area not programmed\r\n");
        return false;
    }

#elif (BOOT_ARCH == BOOT_ARCH_RISCV)
    // RISC-V 架构: 不同工具链的首条指令可能不同，仅验证入口地址与擦除状态
    uint32_t app_first_word = app_word0;
    uint32_t app_entry = app_word1;

    BOOT_LOG("APP Word0: 0x%08X, Entry: 0x%08X\r\n", app_first_word, app_entry);

    // 检查1: 入口地址必须在 APP 区域内
    if (app_entry < BOOT_APP_START_ADDR || app_entry > BOOT_APP_END_ADDR) {
        BOOT_LOG("Invalid entry address\r\n");
        return false;
    }

    // 检查2: 入口地址必须是偶数（2 字节对齐）
    if ((app_entry & 0x1U) != 0U) {
        BOOT_LOG("Entry address not aligned\r\n");
        return false;
    }

    // 检查3: 确保不是擦除默认值（由 BOOT_FLAG_ERASED 定义，未烧录）
    if (app_first_word == BOOT_FLAG_ERASED || app_entry == BOOT_FLAG_ERASED) {
        BOOT_LOG("APP area not programmed (matches erase value 0x%08X)\r\n",
                 BOOT_FLAG_ERASED);
        return false;
    }

#else
    #error "Unsupported architecture: BOOT_ARCH must be BOOT_ARCH_ARM_CORTEX_M or BOOT_ARCH_RISCV"
#endif

    return true;
}

void easy_bootloader_run(void)
{
    if (!g_boot_ctx.initialized) {
        return;
    }

    bootloader_poll_uart();

    uint32_t remaining = 0U;
    uint16_t payload_len = 0U;
    while (bootloader_try_extract_frame(&remaining, &payload_len)) {
        if (bootloader_handle_payload(remaining, payload_len) != BOOT_PORT_OK) {
            BOOT_LOG("bootloader handle payload failed\r\n");
            break;
        }
    }
}

static void bootloader_reset_context(void)
{
    memset(&g_boot_ctx, 0, sizeof(g_boot_ctx));
    g_boot_ctx.current_addr = BOOT_APP_START_ADDR;
}

static void bootloader_poll_uart(void)
{
    // 计算 rx_cache 剩余空间
    uint32_t space = sizeof(g_boot_ctx.rx_cache) - g_boot_ctx.rx_cache_len;
    if (space == 0U) {
        return;  // 缓存已满，等待解析消费
    }

    // 直接从底层读取数据到线性解析缓存
    uint32_t received = boot_port_uart_read(
        &g_boot_ctx.rx_cache[g_boot_ctx.rx_cache_len],
        space
    );

    if (received > 0U) {
        g_boot_ctx.rx_cache_len += (uint16_t)received;
    }
}

static void bootloader_consume_cache(uint16_t count)
{
    if (count >= g_boot_ctx.rx_cache_len) {
        g_boot_ctx.rx_cache_len = 0U;
        return;
    }

    uint16_t remain = g_boot_ctx.rx_cache_len - count;
    memmove(g_boot_ctx.rx_cache, &g_boot_ctx.rx_cache[count], remain);
    g_boot_ctx.rx_cache_len = remain;
}

static bool bootloader_try_extract_frame(uint32_t *remaining, uint16_t *payload_len)
{
    if (g_boot_ctx.rx_cache_len < BOOT_FRAME_FIXED_SIZE) {
        return false;
    }
    //寻找帧头
    while (g_boot_ctx.rx_cache_len >= BOOT_FRAME_FIXED_SIZE) {
        if (g_boot_ctx.rx_cache[0] != BOOT_FRAME_HEADER0 ||
            g_boot_ctx.rx_cache[1] != BOOT_FRAME_HEADER1) {
            bootloader_consume_cache(1U);
            continue;
        }

        uint32_t remain = ((uint32_t)g_boot_ctx.rx_cache[2] << 16) |
                          ((uint32_t)g_boot_ctx.rx_cache[3] << 8) |
                          g_boot_ctx.rx_cache[4];
        uint16_t packet_len = ((uint16_t)g_boot_ctx.rx_cache[5] << 8) |
                              g_boot_ctx.rx_cache[6];

        if (packet_len > BOOT_PAYLOAD_MAX_SIZE) {
            bootloader_consume_cache(2U);
            continue;
        }

        uint32_t frame_size = BOOT_FRAME_FIXED_SIZE + packet_len;
        if (g_boot_ctx.rx_cache_len < frame_size) {
            return false;
        }

        uint32_t checksum_pos = 7U + packet_len;
        uint32_t tail_pos = checksum_pos + 2U;
        if (tail_pos + 1U >= frame_size) {
            bootloader_consume_cache(2U);
            continue;
        }

        uint16_t received_crc = ((uint16_t)g_boot_ctx.rx_cache[checksum_pos] << 8) |
                                g_boot_ctx.rx_cache[checksum_pos + 1U];
        uint16_t calc_crc = 0U;
        for (uint32_t idx = 5U; idx < checksum_pos; idx++) {
            calc_crc += g_boot_ctx.rx_cache[idx];
        }

        if (calc_crc != received_crc ||
            g_boot_ctx.rx_cache[tail_pos] != BOOT_FRAME_TAIL0 ||
            g_boot_ctx.rx_cache[tail_pos + 1U] != BOOT_FRAME_TAIL1) {
            bootloader_consume_cache(2U);
            continue;
        }

        if (packet_len > 0U) {
            memcpy(g_boot_ctx.payload_buf, &g_boot_ctx.rx_cache[7], packet_len);
        }

        *payload_len = packet_len;
        *remaining = remain;
        bootloader_consume_cache((uint16_t)frame_size);
        return true;
    }

    return false;
}

static boot_port_status_t bootloader_write_flag_region(uint32_t flag, uint32_t version, uint32_t date)
{
    // 先擦除标志位区
    boot_port_status_t status = boot_port_flash_erase(BOOT_FLAG_REGION_ADDR, BOOT_FLAG_REGION_SIZE);
    if (status != BOOT_PORT_OK) {
        BOOT_LOG("Erase flag region failed\r\n");
        return status;
    }

    // 写入 flag
    uint8_t buf[4];
    buf[0] = (uint8_t)(flag & 0xFFU);
    buf[1] = (uint8_t)((flag >> 8) & 0xFFU);
    buf[2] = (uint8_t)((flag >> 16) & 0xFFU);
    buf[3] = (uint8_t)((flag >> 24) & 0xFFU);
    status = boot_port_flash_write(BOOT_FLAG_ADDR, buf, 4U);
    if (status != BOOT_PORT_OK) {
        return status;
    }

    // 写入 version
    buf[0] = (uint8_t)(version & 0xFFU);
    buf[1] = (uint8_t)((version >> 8) & 0xFFU);
    buf[2] = (uint8_t)((version >> 16) & 0xFFU);
    buf[3] = (uint8_t)((version >> 24) & 0xFFU);
    status = boot_port_flash_write(BOOT_VERSION_ADDR, buf, 4U);
    if (status != BOOT_PORT_OK) {
        return status;
    }

    // 写入 date
    buf[0] = (uint8_t)(date & 0xFFU);
    buf[1] = (uint8_t)((date >> 8) & 0xFFU);
    buf[2] = (uint8_t)((date >> 16) & 0xFFU);
    buf[3] = (uint8_t)((date >> 24) & 0xFFU);
    return boot_port_flash_write(BOOT_DATE_ADDR, buf, 4U);
}

static boot_port_status_t bootloader_handle_payload(uint32_t remaining, uint16_t payload_len)
{
    boot_port_status_t status = bootloader_prepare_download();
    if (status != BOOT_PORT_OK) {
        return status;
    }

    uint32_t future_bytes = g_boot_ctx.stream_cache_len + payload_len;
    uint32_t worst_case = (future_bytes + 3U) & ~0x3U;  //向上取整到 4 的倍数
    if ((g_boot_ctx.current_addr + worst_case) >
        (BOOT_APP_START_ADDR + BOOT_APP_MAX_SIZE)) {
        BOOT_LOG("flash range overflow\r\n");
        return BOOT_PORT_ERROR;
    }

    status = bootloader_stream_write(g_boot_ctx.payload_buf, payload_len);
    if (status != BOOT_PORT_OK) {
        return status;
    }

    if (remaining == 0U) {
        status = bootloader_stream_flush();
        if (status == BOOT_PORT_OK) {
            g_boot_ctx.download_active = false;
            BOOT_LOG("Download complete, total %lu bytes\r\n",
                          (unsigned long)(g_boot_ctx.current_addr - BOOT_APP_START_ADDR));

            status = bootloader_write_flag_region(BOOT_FLAG_APP,
                                                   g_boot_ctx.app_version,
                                                   g_boot_ctx.update_date);
            if (status == BOOT_PORT_OK) {
                BOOT_LOG("Flag set to APP (ver=0x%08X, date=0x%08X)\r\n",
                              g_boot_ctx.app_version, g_boot_ctx.update_date);

                // 先发送 ACK，再复位
                boot_port_uart_write(g_boot_ack, sizeof(g_boot_ack));
                //BOOT_LOG("ACK sent\r\n");

                BOOT_LOG("Resetting system to run APP...\r\n");

                boot_port_system_reset();  // 软复位，复位后根据 Flag 自动跳 APP
            } else {
                BOOT_LOG("Failed to write flag region\r\n");
            }
        }
        return status;
    }

    if (status == BOOT_PORT_OK) {
        boot_port_uart_write(g_boot_ack, sizeof(g_boot_ack));
        //BOOT_LOG("ACK sent\r\n");
    }

    return status;
}

static boot_port_status_t bootloader_prepare_download(void)
{
    if (g_boot_ctx.download_active) {
        return BOOT_PORT_OK;
    }

    BOOT_LOG("Erasing APP region...\r\n");
    boot_port_status_t status = boot_port_flash_erase(BOOT_APP_START_ADDR, BOOT_APP_MAX_SIZE);
    if (status != BOOT_PORT_OK) {
        BOOT_LOG("Erase failed!\r\n");
        return status;
    }
    BOOT_LOG("Erase done\r\n");

    g_boot_ctx.current_addr = BOOT_APP_START_ADDR;
    g_boot_ctx.stream_cache_len = 0U;
    g_boot_ctx.download_active = true;
    return BOOT_PORT_OK;
}

static boot_port_status_t bootloader_stream_write(const uint8_t *data, uint32_t len)
{
    if (len == 0U) {
        return BOOT_PORT_OK;
    }

    uint32_t offset = 0U;
    if (g_boot_ctx.stream_cache_len > 0U) {         
        while (g_boot_ctx.stream_cache_len < 4U && offset < len) {
            g_boot_ctx.stream_cache[g_boot_ctx.stream_cache_len++] = data[offset++];
        }

        if (g_boot_ctx.stream_cache_len == 4U) {
            boot_port_status_t status = boot_port_flash_write(g_boot_ctx.current_addr,
                                                              g_boot_ctx.stream_cache,
                                                              4U);
            if (status != BOOT_PORT_OK) {
                return status;
            }
            g_boot_ctx.current_addr += 4U;
            g_boot_ctx.stream_cache_len = 0U;
        }
    }

    uint32_t remaining = len - offset;
    uint32_t aligned = remaining & ~0x3U;   // 向下取整到 4 的倍数
    if (aligned > 0U) {
        boot_port_status_t status = boot_port_flash_write(g_boot_ctx.current_addr,
                                                          &data[offset],
                                                          aligned);
        if (status != BOOT_PORT_OK) {
            return status;
        }
        g_boot_ctx.current_addr += aligned;
        offset += aligned;
        remaining -= aligned;
    }

    if (remaining > 0U) {
        memcpy(g_boot_ctx.stream_cache, &data[offset], remaining);
        g_boot_ctx.stream_cache_len = (uint8_t)remaining;
    }
    return BOOT_PORT_OK;
}

static boot_port_status_t bootloader_stream_flush(void)
{
    if (g_boot_ctx.stream_cache_len == 0U) {
        return BOOT_PORT_OK;
    }

    uint8_t padded[4];
    memcpy(padded, g_boot_ctx.stream_cache, g_boot_ctx.stream_cache_len);
    memset(&padded[g_boot_ctx.stream_cache_len], 0xFF, 4U - g_boot_ctx.stream_cache_len);

    boot_port_status_t status = boot_port_flash_write(g_boot_ctx.current_addr, padded, 4U);
    if (status == BOOT_PORT_OK) {
        g_boot_ctx.current_addr += 4U;
        g_boot_ctx.stream_cache_len = 0U;
    }
    return status;
}
