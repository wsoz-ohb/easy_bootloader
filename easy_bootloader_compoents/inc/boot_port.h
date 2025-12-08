//对接接口层头文件
#ifndef BOOT_PORT_H
#define BOOT_PORT_H

#include <stdint.h>

typedef enum {
    BOOT_PORT_OK = 0,
    BOOT_PORT_ERROR = -1,
    BOOT_PORT_TIMEOUT = -2,
} boot_port_status_t;

/**
 * @brief 获取当前系统毫秒时间戳
 */
uint32_t boot_port_get_tick(void);

/**
 * @brief Flash 擦除
 * @param addr 起始地址，框架会保证在 APP 区域内
 * @param size 擦除字节数，需自行处理扇区对齐
 * @return 0 表示成功，其他值表示失败
 */
boot_port_status_t boot_port_flash_erase(uint32_t addr, uint32_t size);

/**
 * @brief Flash 写入
 * @param addr 目标地址（4 字节对齐）
 * @param data 数据指针
 * @param len  长度（4 的倍数）
 */
boot_port_status_t boot_port_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);

/**
 * @brief Flash 读取
 */
boot_port_status_t boot_port_flash_read(uint32_t addr, uint8_t *data, uint32_t len);

/**
 * @brief 串口阻塞发送
 */
boot_port_status_t boot_port_uart_write(const uint8_t *data, uint32_t len);

/**
 * @brief 串口读取（建议非阻塞实现）
 * @param buf     接收缓冲区
 * @param max_len 缓冲区最大长度
 * @return 实际读取的字节数，无数据返回 0
 */
uint32_t boot_port_uart_read(uint8_t *buf, uint32_t max_len);

/**
 * @brief 调试日志输出（可选实现）
 */
void boot_port_log(const char *fmt, ...);

/**
 * @brief 跳转到应用程序
 */
void boot_port_jump_to_app(uint32_t app_addr);

/**
 * @brief 触发系统复位
 */
void boot_port_system_reset(void);

#endif // BOOT_PORT_H
