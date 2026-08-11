# Easy Bootloader 通用框架设计文档

## 1. 设计目标

打造一个**协议固定、底层可移植**的通用 Bootloader 框架：
- 用户只需实现底层硬件接口（Flash、串口等）
- 应用层协议、状态机、流程控制完全复用
- 支持不同 MCU（STM32F1/F4/H7、GD32、CH32 等）

---

## 2. 架构分层

```
┌────────────────────────────────────────────────────────┐
│                    用户应用 (main.c)                    │
│              调用 easy_bootloader_init/run              │
├────────────────────────────────────────────────────────┤
│                 应用层 (easy_bootloader.c)              │
│    协议解析 | 状态机 | 数据流写入 | 跳转逻辑 | ACK 应答   │
├────────────────────────────────────────────────────────┤
│                 移植层 (boot_port.h)                    │
│         定义底层接口，用户根据 MCU 实现                   │
├────────────────────────────────────────────────────────┤
│                 底层实现 (boot_port_xxx.c)              │
│    STM32F4 实现 | STM32F1 实现 | GD32 实现 | ...        │
└────────────────────────────────────────────────────────┘
```

---

## 3. 文件结构

```
easy_bootloader_compoents/
├── inc/
│   ├── easy_bootloader.h      # 应用层对外 API
│   ├── boot_port.h            # 移植层接口定义（用户需实现）
│   └── boot_config.h          # 配置宏定义
├── src/
│   ├── easy_bootloader.c      # 应用层实现（通用，不改）
│   └── boot_protocol.c        # 协议解析（通用，不改）
├── ports/
│   ├── stm32f4xx/
│   │   └── boot_port_stm32f4.c
│   ├── stm32f1xx/
│   │   └── boot_port_stm32f1.c
│   └── template/
│       └── boot_port_template.c  # 移植模板
├── examples/
│   └── stm32f407_example/     # 完整示例工程
└── DESIGN.md                  # 本文档
```

---

## 4. 移植层接口定义

用户移植时**必须实现**以下接口：

### 4.1 Flash 操作接口

```c
/**
 * @brief 擦除 Flash 指定区域
 * @param addr   起始地址
 * @param size   擦除大小（字节）
 * @return 0=成功, 其他=失败
 * @note  框架会确保 addr 在 APP 区域内
 *        实现时需处理扇区对齐问题
 */
int boot_port_flash_erase(uint32_t addr, uint32_t size);

/**
 * @brief 写入 Flash
 * @param addr   目标地址（框架保证 4 字节对齐）
 * @param data   数据指针
 * @param len    数据长度（字节，框架保证 4 的倍数）
 * @return 0=成功, 其他=失败
 * @note  写入前框架已调用 erase，无需重复擦除
 */
int boot_port_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);

/**
 * @brief 读取 Flash
 * @param addr   源地址
 * @param data   目标缓冲区
 * @param len    读取长度
 * @return 0=成功, 其他=失败
 */
int boot_port_flash_read(uint32_t addr, uint8_t *data, uint32_t len);
```

### 4.2 串口操作接口

```c
/**
 * @brief 从串口读取数据（非阻塞）
 * @param buf      接收缓冲区
 * @param max_len  缓冲区最大长度
 * @return 实际读取的字节数，无数据返回 0
 * @note  建议底层使用 DMA + 环形缓冲实现
 */
uint32_t boot_port_uart_read(uint8_t *buf, uint32_t max_len);

/**
 * @brief 向串口发送数据（阻塞）
 * @param data  数据指针
 * @param len   数据长度
 * @return 0=成功, 其他=失败
 */
int boot_port_uart_write(const uint8_t *data, uint32_t len);
```

### 4.3 系统操作接口

```c
/**
 * @brief 跳转到 APP
 * @param app_addr  APP 起始地址
 * @note  实现要点：
 *        1. 关闭所有中断
 *        2. 复位所有外设
 *        3. 设置 MSP
 *        4. 设置 VTOR（如果有）
 *        5. 跳转到复位向量
 */
void boot_port_jump_to_app(uint32_t app_addr);

/**
 * @brief 系统复位
 */
void boot_port_system_reset(void);

/**
 * @brief 获取系统时间（毫秒）
 * @return 毫秒级时间戳，用于超时判断
 */
uint32_t boot_port_get_tick(void);
```

### 4.4 日志接口（可选）

```c
/**
 * @brief 调试日志输出
 * @note  可实现为空函数，或输出到调试串口
 */
void boot_port_log(const char *fmt, ...);
```

---

## 5. 配置项设计

用户通过 `boot_config.h` 或结构体配置：

### 5.1 必须配置项

| 配置项 | 说明 | 示例值 (STM32F407) |
|--------|------|-------------------|
| `BOOT_APP_START_ADDR` | APP 起始地址 | `0x08010000` |
| `BOOT_APP_MAX_SIZE` | APP 最大尺寸 | `0x000D0000` (832KB) |
| `BOOT_FLAG_ADDR` | 标志位存储地址 | `0x080E0000` |
| `BOOT_SRAM_START` | SRAM 起始地址 | `0x20000000` |
| `BOOT_SRAM_END` | SRAM 结束地址 | `0x20030000` |

### 5.2 可选配置项

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `BOOT_PACKET_MAX_SIZE` | 单包最大数据长度 | `1013` |
| `BOOT_UART_TIMEOUT_MS` | 串口超时时间 | `5000` |
| `BOOT_ENABLE_LOG` | 是否启用日志 | `1` |
| `BOOT_ENABLE_CRC32` | 是否启用 CRC32 校验 | `0` |

### 5.3 配置方式

**方式一：宏定义（简单）**
```c
// boot_config.h
#define BOOT_APP_START_ADDR   0x08010000
#define BOOT_APP_MAX_SIZE     0x000D0000
#define BOOT_FLAG_ADDR        0x080E0000
// ...
```

**方式二：结构体（灵活）**
```c
// 用户代码中
static const boot_config_t g_boot_cfg = {
    .app_start_addr = 0x08010000,
    .app_max_size   = 0x000D0000,
    .flag_addr      = 0x080E0000,
    // ...
};
easy_bootloader_init(&g_boot_cfg);
```

**建议**：采用宏定义方式，简单直接，编译期确定，节省 RAM。

---

## 6. 不同 MCU 的适配要点

### 6.1 Flash 差异处理

| MCU 系列 | Flash 特点 | 适配要点 |
|----------|-----------|---------|
| STM32F1 | 页式擦除（1KB/2KB） | 按页擦除，写入粒度 2 字节 |
| STM32F4 | 扇区式擦除（16KB~128KB） | 按扇区擦除，写入粒度 1/2/4 字节可选 |
| STM32H7 | 扇区式（128KB），双 Bank | 注意 Bank 切换，写入粒度 32 字节 |
| GD32F4 | 类似 STM32F4 | 基本兼容，注意时序差异 |
| CH32V3 | 页式擦除（256B） | 页较小，擦除频繁 |

**框架处理策略**：
- 框架只关心"地址+长度"，不关心扇区/页的概念
- `boot_port_flash_erase()` 由用户实现扇区/页的映射
- 框架保证写入地址 4 字节对齐，长度 4 的倍数

### 6.2 扇区映射表设计

用户需要在移植层维护扇区信息：

```c
// boot_port_stm32f4.c 示例
typedef struct {
    uint32_t start;      // 扇区起始地址
    uint32_t size;       // 扇区大小
    uint32_t sector_id;  // HAL 库扇区 ID
    uint8_t  erased;     // 是否已擦除（运行时状态）
} sector_info_t;

static sector_info_t g_app_sectors[] = {
    {0x08010000,  64*1024, FLASH_SECTOR_4,  0},
    {0x08020000, 128*1024, FLASH_SECTOR_5,  0},
    {0x08040000, 128*1024, FLASH_SECTOR_6,  0},
    // ...
};
```

### 6.3 写入粒度处理

| MCU | 最小写入单位 | 框架要求 |
|-----|-------------|---------|
| STM32F1 | 2 字节（半字） | 框架传入 4 字节对齐数据，底层拆分 |
| STM32F4 | 1/2/4 字节可选 | 直接使用 4 字节写入 |
| STM32H7 | 32 字节（Flash Word） | 底层需要凑满 32 字节再写 |

**STM32H7 特殊处理示例**：
```c
// boot_port_stm32h7.c
static uint8_t write_buffer[32];
static uint8_t write_buffer_len = 0;

int boot_port_flash_write(uint32_t addr, const uint8_t *data, uint32_t len) {
    // 凑满 32 字节再调用 HAL_FLASH_Program
    // 最后 flush 时不足 32 字节用 0xFF 填充
}
```

---

## 7. 常见问题与解决方案

### 7.1 Flash 写入前未擦除

**问题**：Flash 只能从 1 写成 0，不能从 0 写成 1。

**解决**：
- 框架维护"已擦除扇区"标记数组
- 写入前检查目标扇区是否已擦除
- 未擦除则先擦除再写入

```c
// 框架内部逻辑
if (!is_sector_erased(addr)) {
    boot_port_flash_erase(sector_start, sector_size);
    mark_sector_erased(addr);
}
boot_port_flash_write(addr, data, len);
```

### 7.2 跳转后 APP 无法运行

**常见原因**：
1. 中断未关闭 → 跳转前 `__disable_irq()`
2. 外设未复位 → 调用 `HAL_DeInit()` 等
3. SysTick 仍在运行 → 清除 SysTick 寄存器
4. VTOR 未设置 → `SCB->VTOR = app_addr`
5. APP 链接地址错误 → 检查 APP 的链接脚本

**标准跳转流程**：
```c
void boot_port_jump_to_app(uint32_t app_addr) {
    uint32_t app_stack = *(uint32_t *)app_addr;
    uint32_t app_reset = *(uint32_t *)(app_addr + 4);

    // 1. 校验栈指针和复位向量
    if (!is_valid_stack(app_stack) || !is_valid_reset(app_reset)) {
        return;  // APP 无效，不跳转
    }

    // 2. 关闭中断
    __disable_irq();

    // 3. 复位外设
    // HAL_UART_DeInit(...);
    // HAL_DMA_DeInit(...);
    // HAL_RCC_DeInit();
    // HAL_DeInit();

    // 4. 清除 SysTick
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    // 5. 设置 MSP 和 VTOR
    __set_MSP(app_stack);
    SCB->VTOR = app_addr;

    // 6. 内存屏障
    __DSB();
    __ISB();

    // 7. 跳转
    ((void (*)(void))app_reset)();
}
```

### 7.3 升级中断电导致变砖

**问题**：写入过程中断电，标志位已改但固件不完整。

**解决方案**：

**方案 A：写入完成后再改标志位（当前方案）**
```
1. 收到升级命令 → 设置 flag=1（Bootloader 模式）
2. 接收并写入固件
3. 写入完成 → 设置 flag=2（APP 模式）
4. 跳转到 APP
```
- 优点：简单
- 缺点：中途断电后 flag=1，需要重新刷写

**方案 B：双标志位**
```
flag1: 升级状态 (0=空闲, 1=升级中, 2=升级完成)
flag2: 固件 CRC32
```
- 启动时检查：flag1=2 且 CRC 正确才跳转
- 否则停留在 Bootloader

**方案 C：双区镜像（推荐，但复杂）**
```
区域 A: 当前运行固件
区域 B: 新固件写入区
```
- 新固件写入 B 区，校验通过后切换启动区
- 任何时候都有一份完整固件

**建议**：初期用方案 A，后续可扩展为方案 B。

### 7.4 大小端问题

**问题**：协议中多字节字段的解析。

**当前协议**：大端序（高字节在前）
```c
// 剩余字节（3 字节，大端）
uint32_t remaining = ((uint32_t)data[2] << 16) |
                     ((uint32_t)data[3] << 8)  |
                     (uint32_t)data[4];
```

**建议**：框架内部统一处理，用户无需关心。

### 7.5 串口缓冲区溢出

**问题**：数据包到达速度超过处理速度。

**解决**：
- 底层使用 DMA + 环形缓冲区
- 缓冲区大小建议 >= 2 * 最大包长度
- 框架处理完一包后再读取下一包

```c
// 建议的缓冲区大小
#define UART_RX_BUF_SIZE  2048  // >= 2 * (1024 包最大长度)
```

### 7.6 超时处理

**问题**：上位机发送中断，Bootloader 卡死。

**解决**：
```c
// 框架内部
static uint32_t last_rx_tick = 0;

void easy_bootloader_run(void) {
    uint32_t now = boot_port_get_tick();

    if (has_received_data) {
        last_rx_tick = now;
        // 处理数据...
    }

    // 超时检测（例如 30 秒无数据）
    if (is_downloading && (now - last_rx_tick > 30000)) {
        // 超时，复位下载状态
        reset_download_state();
        boot_port_log("Download timeout, reset\r\n");
    }
}
```

### 7.7 版本回退保护

**问题**：误刷旧版本固件。

**解决**：
```c
// 升级命令中携带版本号
// 框架检查：新版本 > 当前版本 才允许升级

if (new_version <= current_version) {
    boot_port_log("Version rollback rejected\r\n");
    return BOOT_ERR_VERSION;
}
```

**可选**：通过配置项 `BOOT_ALLOW_ROLLBACK` 控制是否允许回退。

---

## 8. 应用层 API 设计

### 8.1 对外接口

```c
// easy_bootloader.h

/**
 * @brief 初始化 Bootloader
 * @note  读取标志位，决定是否跳转到 APP
 *        如果 flag=APP_MODE 且 APP 有效，直接跳转
 *        否则停留在 Bootloader 模式
 */
void easy_bootloader_init(void);

/**
 * @brief Bootloader 主循环
 * @note  在 main 的 while(1) 中调用
 *        处理串口数据、协议解析、Flash 写入
 */
void easy_bootloader_run(void);

/**
 * @brief 获取当前状态
 * @return BOOT_STATE_IDLE / BOOT_STATE_DOWNLOADING / BOOT_STATE_DONE
 */
boot_state_t easy_bootloader_get_state(void);

/**
 * @brief 手动触发跳转到 APP
 * @return 0=成功跳转, -1=APP 无效
 */
int easy_bootloader_jump(void);
```

### 8.2 使用示例

```c
// main.c
#include "easy_bootloader.h"

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();

    easy_bootloader_init();  // 可能直接跳转到 APP

    while (1) {
        easy_bootloader_run();
    }
}
```

---

## 9. 移植步骤清单

### Step 1: 复制模板
```
复制 ports/template/boot_port_template.c
重命名为 boot_port_xxx.c（xxx 为你的 MCU）
```

### Step 2: 配置参数
```
编辑 boot_config.h，填写：
- APP 起始地址
- APP 最大尺寸
- 标志位地址
- SRAM 范围
```

### Step 3: 实现 Flash 接口
```
实现 boot_port_flash_erase()
实现 boot_port_flash_write()
实现 boot_port_flash_read()
```

### Step 4: 实现串口接口
```
实现 boot_port_uart_read()  - 建议用 DMA + 环形缓冲
实现 boot_port_uart_write() - 阻塞发送即可
```

### Step 5: 实现系统接口
```
实现 boot_port_jump_to_app()
实现 boot_port_system_reset()
实现 boot_port_get_tick()
```

### Step 6: 集成测试
```
1. 编译 Bootloader，烧录到 MCU
2. 使用 PC 工具发送测试固件
3. 验证跳转是否正常
4. 测试异常情况（断电、超时等）
```

---

## 10. 后续扩展方向

| 功能 | 优先级 | 说明 |
|------|--------|------|
| CRC32 校验 | 高 | 增强数据完整性 |
| 加密传输 | 中 | AES 加密固件 |
| 签名验证 | 中 | 防止非法固件 |
| 双区镜像 | 中 | 支持回退 |
| 多通道支持 | 低 | USB/CAN/蓝牙 |
| 压缩传输 | 低 | 减少传输时间 |

---

## 11. 参考资料

- 当前 STM32F407 实现：`my_bootloader/Compoents/easy_bootloader.c`
- 协议定义：`my_bootloader/Compoents/easy_bootloader.h`
- Flash 操作：`my_bootloader/Compoents/myflash.c`
