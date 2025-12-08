// APP 应用层源文件
#include "easy_bootloader_app.h"
#include "boot_port_app.h"
#include "boot_config_app.h"

#include <stdbool.h>
#include <string.h>

/* 应用层日志封装，受 BOOT_APP_CONFIG_ENABLE_LOG 宏控制 */
#if BOOT_APP_CONFIG_ENABLE_LOG
    #define BOOT_APP_LOG(fmt, ...)  boot_port_app_log(fmt, ##__VA_ARGS__)
#else
    #define BOOT_APP_LOG(fmt, ...)  ((void)0)
#endif

/* 帧格式常量 */
#define BOOT_FRAME_HEADER0        0x55U
#define BOOT_FRAME_HEADER1        0xAAU
#define BOOT_FRAME_TAIL0          0x55U
#define BOOT_FRAME_TAIL1          0x55U

/* 命令帧长度 */
#define CMD_QUERY_VERSION_LEN     6U    // 55 AA FF DD 55 55
#define CMD_QUERY_DATE_LEN        6U    // 55 AA FF CC 55 55
#define CMD_START_FLASH_LEN       14U   // 55 AA [ver 4B] [date 4B] FF EE 55 55

/* 命令特征字节 */
#define CMD_QUERY_VERSION_BYTE0   0xFFU
#define CMD_QUERY_VERSION_BYTE1   0xDDU
#define CMD_QUERY_DATE_BYTE0      0xFFU
#define CMD_QUERY_DATE_BYTE1      0xCCU
#define CMD_START_FLASH_BYTE0     0xFFU
#define CMD_START_FLASH_BYTE1     0xEEU

/* 标志位值 */
#define BOOT_FLAG_BOOTLOADER      1U
#define BOOT_FLAG_APP             2U

/* ACK 应答帧 */
static const uint8_t g_boot_ack[] = {0x55U, 0xAAU, 0xFFU, 0xFEU, 0x55U, 0x55U};

/* 命令类型枚举 */
typedef enum {
    BL_APP_CMD_NONE = 0,
    BL_APP_CMD_QUERY_VERSION = 1,
    BL_APP_CMD_QUERY_DATE = 2,
    BL_APP_CMD_START_FLASH = 3
} bl_app_cmd_t;

/* APP 上下文结构体 */
typedef struct {
    uint8_t  rx_cache[CMD_START_FLASH_LEN * 2];  // 线性解析缓存（最大命令长度的2倍）
    uint16_t rx_cache_len;

    uint32_t boot_flag;
    uint32_t app_version;
    uint32_t update_date;
    bool initialized;
} bootloader_app_context_t;

static bootloader_app_context_t g_app_ctx;

/* 内部函数声明 */
static void app_reset_context(void);
static void app_read_flag_region(void);
static void app_poll_uart(void);
static void app_consume_cache(uint16_t count);
static bl_app_cmd_t app_check_dataframe(uint32_t *version, uint32_t *date);
static void app_handle_query_version(void);
static void app_handle_query_date(void);
static void app_handle_start_flash(uint32_t new_version, uint32_t new_date);
static boot_port_app_status_t app_write_flag_region(uint32_t flag, uint32_t version, uint32_t date);
static void app_send_string(const char *str);
static void app_uint_to_str(uint32_t value, char *buf, uint8_t width);

void easy_bootloader_app_init(void)
{
    BOOT_APP_LOG("=== Easy Bootloader APP Start ===\r\n");

    app_reset_context();
    app_read_flag_region();

    BOOT_APP_LOG("Current Version: 0x%08X, Date: 0x%08X\r\n",
                  g_app_ctx.app_version, g_app_ctx.update_date);

    g_app_ctx.initialized = true;
    BOOT_APP_LOG("APP ready, waiting for commands...\r\n");
}

void easy_bootloader_app_run(void)
{
    if (!g_app_ctx.initialized) {
        return;
    }

    app_poll_uart();

    uint32_t new_version = 0U;
    uint32_t new_date = 0U;
    bl_app_cmd_t cmd = app_check_dataframe(&new_version, &new_date);

    switch (cmd) {
        case BL_APP_CMD_QUERY_VERSION:
            app_handle_query_version();
            break;

        case BL_APP_CMD_QUERY_DATE:
            app_handle_query_date();
            break;

        case BL_APP_CMD_START_FLASH:
            app_handle_start_flash(new_version, new_date);
            break;

        case BL_APP_CMD_NONE:
        default:
            break;
    }
}

static void app_reset_context(void)
{
    memset(&g_app_ctx, 0, sizeof(g_app_ctx));
}

static void app_read_flag_region(void)
{
    boot_port_app_flash_read(BOOT_APP_FLAG_ADDR, (uint8_t *)&g_app_ctx.boot_flag, 4U);
    boot_port_app_flash_read(BOOT_APP_VERSION_ADDR, (uint8_t *)&g_app_ctx.app_version, 4U);
    boot_port_app_flash_read(BOOT_APP_DATE_ADDR, (uint8_t *)&g_app_ctx.update_date, 4U);
}

static void app_poll_uart(void)
{
    // 计算 rx_cache 剩余空间
    uint32_t space = sizeof(g_app_ctx.rx_cache) - g_app_ctx.rx_cache_len;
    if (space == 0U) {
        return;  // 缓存已满，等待解析消费
    }

    // 直接从底层读取数据到线性解析缓存
    uint32_t received = boot_port_app_uart_read(
        &g_app_ctx.rx_cache[g_app_ctx.rx_cache_len],
        space
    );

    if (received > 0U) {
        g_app_ctx.rx_cache_len += (uint16_t)received;
    }
}

static void app_consume_cache(uint16_t count)
{
    if (count >= g_app_ctx.rx_cache_len) {
        g_app_ctx.rx_cache_len = 0U;
        return;
    }

    uint16_t remain = g_app_ctx.rx_cache_len - count;
    memmove(g_app_ctx.rx_cache, &g_app_ctx.rx_cache[count], remain);
    g_app_ctx.rx_cache_len = remain;
}

/**
 * @brief 解析数据帧，识别命令类型
 * @param version 输出参数，触发升级命令时返回新版本号
 * @param date    输出参数，触发升级命令时返回新更新时间
 * @return 命令类型
 */
static bl_app_cmd_t app_check_dataframe(uint32_t *version, uint32_t *date)
{
    /* 最小帧长度检查 */
    if (g_app_ctx.rx_cache_len < CMD_QUERY_VERSION_LEN) {
        return BL_APP_CMD_NONE;
    }

    /* 查找帧头 */
    while (g_app_ctx.rx_cache_len >= CMD_QUERY_VERSION_LEN) {
        if (g_app_ctx.rx_cache[0] != BOOT_FRAME_HEADER0 ||
            g_app_ctx.rx_cache[1] != BOOT_FRAME_HEADER1) {
            app_consume_cache(1U);
            continue;
        }

        /* 检查查询版本命令: 55 AA FF DD 55 55 (6字节) */
        if (g_app_ctx.rx_cache_len >= CMD_QUERY_VERSION_LEN) {
            if (g_app_ctx.rx_cache[2] == CMD_QUERY_VERSION_BYTE0 &&
                g_app_ctx.rx_cache[3] == CMD_QUERY_VERSION_BYTE1 &&
                g_app_ctx.rx_cache[4] == BOOT_FRAME_TAIL0 &&
                g_app_ctx.rx_cache[5] == BOOT_FRAME_TAIL1) {
                app_consume_cache(CMD_QUERY_VERSION_LEN);
                return BL_APP_CMD_QUERY_VERSION;
            }
        }

        /* 检查查询更新时间命令: 55 AA FF CC 55 55 (6字节) */
        if (g_app_ctx.rx_cache_len >= CMD_QUERY_DATE_LEN) {
            if (g_app_ctx.rx_cache[2] == CMD_QUERY_DATE_BYTE0 &&
                g_app_ctx.rx_cache[3] == CMD_QUERY_DATE_BYTE1 &&
                g_app_ctx.rx_cache[4] == BOOT_FRAME_TAIL0 &&
                g_app_ctx.rx_cache[5] == BOOT_FRAME_TAIL1) {
                app_consume_cache(CMD_QUERY_DATE_LEN);
                return BL_APP_CMD_QUERY_DATE;
            }
        }

        /* 检查触发升级命令: 55 AA [ver 4B] [date 4B] FF EE 55 55 (14字节) */
        if (g_app_ctx.rx_cache_len >= CMD_START_FLASH_LEN) {
            if (g_app_ctx.rx_cache[10] == CMD_START_FLASH_BYTE0 &&
                g_app_ctx.rx_cache[11] == CMD_START_FLASH_BYTE1 &&
                g_app_ctx.rx_cache[12] == BOOT_FRAME_TAIL0 &&
                g_app_ctx.rx_cache[13] == BOOT_FRAME_TAIL1) {
                /* 解析版本号 (大端序) */
                *version = ((uint32_t)g_app_ctx.rx_cache[2] << 24) |
                           ((uint32_t)g_app_ctx.rx_cache[3] << 16) |
                           ((uint32_t)g_app_ctx.rx_cache[4] << 8)  |
                           (uint32_t)g_app_ctx.rx_cache[5];
                /* 解析更新时间 (大端序) */
                *date = ((uint32_t)g_app_ctx.rx_cache[6] << 24) |
                        ((uint32_t)g_app_ctx.rx_cache[7] << 16) |
                        ((uint32_t)g_app_ctx.rx_cache[8] << 8)  |
                        (uint32_t)g_app_ctx.rx_cache[9];
                app_consume_cache(CMD_START_FLASH_LEN);
                return BL_APP_CMD_START_FLASH;
            }
        }

        /* 帧头匹配但命令不匹配，跳过帧头继续查找 */
        app_consume_cache(2U);
    }

    return BL_APP_CMD_NONE;
}

/**
 * @brief 发送字符串到串口
 * @param str 要发送的字符串
 */
static void app_send_string(const char *str)
{
    boot_port_app_uart_write((const uint8_t *)str, strlen(str));
}

/**
 * @brief 简单的整数转字符串
 * @param value 整数值
 * @param buf   输出缓冲区
 * @param width 最小宽度，不足补0
 */
static void app_uint_to_str(uint32_t value, char *buf, uint8_t width)
{
    char temp[12];
    int i = 0;

    if (value == 0) {
        temp[i++] = '0';
    } else {
        while (value > 0) {
            temp[i++] = '0' + (value % 10);
            value /= 10;
        }
    }

    /* 补0 */
    while (i < width) {
        temp[i++] = '0';
    }

    /* 反转 */
    for (int j = 0; j < i; j++) {
        buf[j] = temp[i - 1 - j];
    }
    buf[i] = '\0';
}

/**
 * @brief 处理查询版本命令
 */
static void app_handle_query_version(void)
{
    BOOT_APP_LOG("Query version command received\r\n");

    /* 格式化版本号: version:xx */
    char buf[32];
    char num_str[12];

    strcpy(buf, "version:");
    app_uint_to_str(g_app_ctx.app_version, num_str, 1);
    strcat(buf, num_str);
    strcat(buf, "\r\n");

    app_send_string(buf);
    BOOT_APP_LOG("Version: %lu\r\n", (unsigned long)g_app_ctx.app_version);
}

/**
 * @brief 处理查询更新时间命令
 */
static void app_handle_query_date(void)
{
    BOOT_APP_LOG("Query date command received\r\n");

    /* 解析日期: 0xYYYYMMDD -> YYYY-MM-DD */
    uint32_t date = g_app_ctx.update_date;
    uint16_t year = (date >> 16) & 0xFFFF;
    uint8_t month = (date >> 8) & 0xFF;
    uint8_t day = date & 0xFF;

    /* 格式化: YYYY-MM-DD */
    char buf[32];
    char num_str[8];

    app_uint_to_str(year, num_str, 4);
    strcpy(buf, num_str);
    strcat(buf, "-");

    app_uint_to_str(month, num_str, 2);
    strcat(buf, num_str);
    strcat(buf, "-");

    app_uint_to_str(day, num_str, 2);
    strcat(buf, num_str);
    strcat(buf, "\r\n");

    app_send_string(buf);
    BOOT_APP_LOG("Date: %s", buf);
}

/**
 * @brief 处理触发升级命令
 * @param new_version 新版本号
 * @param new_date    新更新时间
 */
static void app_handle_start_flash(uint32_t new_version, uint32_t new_date)
{
    BOOT_APP_LOG("Start flash command received\r\n");
    BOOT_APP_LOG("New Version: 0x%08X, New Date: 0x%08X\r\n", new_version, new_date);

    /* 比较版本号 */
    if (new_version == g_app_ctx.app_version) {
        BOOT_APP_LOG("Version is same, don't need to update\r\n");
        return;
    }

    BOOT_APP_LOG("Version different, starting upgrade...\r\n");

    /* 发送 ACK 应答 */
    boot_port_app_uart_write(g_boot_ack, sizeof(g_boot_ack));
    BOOT_APP_LOG("ACK sent\r\n");

    /* 写入新版本号、更新时间和标志位 */
    boot_port_app_status_t status = app_write_flag_region(
        BOOT_FLAG_BOOTLOADER,
        new_version,
        new_date);
    if (status != BOOT_PORT_APP_OK) {
        BOOT_APP_LOG("Write flag region failed\r\n");
        return;
    }

    BOOT_APP_LOG("Flag set to BOOTLOADER, resetting...\r\n");

    /* 系统复位，进入 Bootloader 模式 */
    boot_port_app_system_reset();
}

/**
 * @brief 写入标志位区
 * @param flag    启动标志
 * @param version 版本号
 * @param date    更新时间
 * @return 操作状态
 */
static boot_port_app_status_t app_write_flag_region(uint32_t flag, uint32_t version, uint32_t date)
{
    /* 先擦除标志位区 */
    boot_port_app_status_t status = boot_port_app_flash_erase(BOOT_APP_FLAG_REGION_ADDR, BOOT_APP_FLAG_REGION_SIZE);
    if (status != BOOT_PORT_APP_OK) {
        BOOT_APP_LOG("Erase flag region failed\r\n");
        return status;
    }

    /* 写入 flag */
    uint8_t buf[4];
    buf[0] = (uint8_t)(flag & 0xFFU);
    buf[1] = (uint8_t)((flag >> 8) & 0xFFU);
    buf[2] = (uint8_t)((flag >> 16) & 0xFFU);
    buf[3] = (uint8_t)((flag >> 24) & 0xFFU);
    status = boot_port_app_flash_write(BOOT_APP_FLAG_ADDR, buf, 4U);
    if (status != BOOT_PORT_APP_OK) {
        return status;
    }

    /* 写入 version */
    buf[0] = (uint8_t)(version & 0xFFU);
    buf[1] = (uint8_t)((version >> 8) & 0xFFU);
    buf[2] = (uint8_t)((version >> 16) & 0xFFU);
    buf[3] = (uint8_t)((version >> 24) & 0xFFU);
    status = boot_port_app_flash_write(BOOT_APP_VERSION_ADDR, buf, 4U);
    if (status != BOOT_PORT_APP_OK) {
        return status;
    }

    /* 写入 date */
    buf[0] = (uint8_t)(date & 0xFFU);
    buf[1] = (uint8_t)((date >> 8) & 0xFFU);
    buf[2] = (uint8_t)((date >> 16) & 0xFFU);
    buf[3] = (uint8_t)((date >> 24) & 0xFFU);
    /* 写入 date */
    buf[0] = (uint8_t)(date & 0xFFU);
    buf[1] = (uint8_t)((date >> 8) & 0xFFU);
    buf[2] = (uint8_t)((date >> 16) & 0xFFU);
    buf[3] = (uint8_t)((date >> 24) & 0xFFU);
    status = boot_port_app_flash_write(BOOT_APP_DATE_ADDR, buf, 4U);
    if (status != BOOT_PORT_APP_OK) {
        return status;
    }

    return boot_port_app_flash_write(BOOT_APP_DATE_ADDR, buf, 4U);
}
