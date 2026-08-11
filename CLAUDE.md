# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

通用 Bootloader 框架，用于 STM32/CH32 系列 MCU 的固件在线升级（OTA）。协议固定、底层可移植，支持 ARM Cortex-M (`BOOT_ARCH_ARM_CORTEX_M`) 和 RISC-V (`BOOT_ARCH_RISCV`) 架构。

## 构建工程

### STM32F4 (Keil MDK-ARM)
- **Bootloader**: `stm32f4_example/easy_bootloader_boot/project/MDK-ARM/project.uvprojx`
- **APP**: `stm32f4_example/easy_bootloader_app/project/MDK-ARM/project.uvprojx`

### CH32V307 (MounRiver Studio)
- **Bootloader**: `ch32v307_example/CH32V307VCT6_bootloader_project/.project`
- **APP**: `ch32v307_example/CH32V307VCT6_app_project/.project`
- 烧录器: WCH-LINK

### 上位机工具
```bash
pip install pyserial
python serial_terminal.py
```

## 架构分层

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

## 核心目录与组件同步

| 目录 | 说明 |
|------|------|
| `easy_bootloader_compoents/` | Bootloader 组件库（核心源码） |
| `easy_bootloader_app_compoents/` | APP 端组件库（处理升级触发） |
| `stm32f4_example/` | STM32F407 示例工程 (Keil) |
| `ch32v307_example/` | CH32V307 示例工程 (MounRiver, RISC-V) |

**⚠️ 组件同步规则**: 修改 `easy_bootloader_compoents/` 或 `easy_bootloader_app_compoents/` 后，**必须手动同步**到示例工程的 `Compoents/` / `Components/` 目录。工程目录额外包含 `ringbuffer.c/h`。

## Bootloader 启动决策逻辑

```
easy_bootloader_init()
    │
    ├─ flag == BOOT_FLAG_BOOTLOADER (1)
    │       → 停留在 Bootloader，等待固件
    │
    ├─ flag == BOOT_FLAG_APP (2)
    │       → 检查 APP 有效性
    │           ├─ 有效 → 跳转到 APP
    │           └─ 无效 → 停留在 Bootloader
    │
    └─ flag == BOOT_FLAG_ERASED (0xFFFFFFFF)
            → 检查 APP 有效性
                ├─ 有效 → 停留在 Bootloader（等待首次升级）
                └─ 无效 → 停留在 Bootloader
```

## APP 有效性检查

`bootloader_check_app_valid()` 执行以下检查：

1. **栈指针检查**: APP 起始地址的第一个 word 必须在 SRAM/CCM 范围内
2. **复位向量检查**: 第二个 word 必须在 APP 区域内
3. **架构对齐检查**:
   - ARM Cortex-M: 复位向量必须是奇数（Thumb 模式）
   - RISC-V: 复位向量必须是偶数（2 字节对齐）
4. **非空检查**: 栈指针和复位向量不能是 `0xFFFFFFFF`

## Flash 布局配置

在 `boot_config.h` 中定义。

### STM32F407

| 区域 | 起始地址 | 大小 |
|------|----------|------|
| Bootloader | `0x08000000` | 64KB |
| APP | `0x08010000` | 832KB |
| 标志位区 | `0x080E0000` | 128KB |

**SRAM/CCM 配置**:
- SRAM: `0x20000000` - `0x20030000`
- CCM: `0x10000000` - `0x10010000` (通过 `BOOT_HAS_CCM` 宏控制)

### CH32V307

| 区域 | 别名地址 | 物理地址 | 大小 |
|------|----------|----------|------|
| Bootloader | `0x00000000` | `0x08000000` | 24KB |
| APP | `0x00006000` | `0x08006000` | 230KB |
| 标志位区 | `0x0003F800` | `0x0803F800` | 2KB |

**重要**: CH32 使用别名地址 `0x0000_xxxx`，物理地址 `0x0800_xxxx`；Flash 擦除默认值为 `0xE339E339`（非 0xFFFFFFFF）。

### 标志位区布局 (通用)
| 偏移 | 内容 | 说明 |
|------|------|------|
| 0x00 | bootloader_flag | 启动标志 (1=Bootloader模式, 2=APP模式) |
| 0x04 | app_version | 应用版本号 |
| 0x08 | update_date | 更新日期 (格式: 0xYYYYMMDD) |

## 串口协议格式

```
帧头(2B) + 剩余字节(3B) + 长度(2B) + 数据(nB) + 校验(2B) + 帧尾(2B)
0x55 0xAA                                                    0x55 0x55
```

- 最大包长度: 1013 字节
- 剩余字节: 大端序，表示后续还有多少字节待传输（为 0 表示最后一包）
- 校验: 从长度字段开始到数据结束的累加和

**ACK 应答格式**:
```
0x55 0xAA 0xFF 0xFE 0x55 0x55
```

**完成帧格式** (刷写完成后上位机发送):
```
帧头(2B) + 版本号(4B) + 更新日期(4B) + 命令码(2B) + 帧尾(2B)
0x55 0xAA  [version]    [0xYYYYMMDD]   0xFF 0xFD    0x55 0x55
```

## APP 端命令协议

APP 端接收上位机命令，支持以下操作：

| 命令 | 格式 | 说明 |
|------|------|------|
| 查询版本 | `55 AA FF DD 55 55` | 返回 `version:xx\r\n` |
| 查询日期 | `55 AA FF CC 55 55` | 返回 `YYYY-MM-DD\r\n` |
| 触发升级 | `55 AA [ver 4B] [date 4B] FF EE 55 55` | 版本不同时发送 ACK 并复位进入 Bootloader |

## 移植层接口

返回值类型 `boot_port_status_t`: `BOOT_PORT_OK(0)`, `BOOT_PORT_ERROR(-1)`, `BOOT_PORT_TIMEOUT(-2)`

### Bootloader 端 (`boot_port.h`)

- `boot_port_get_tick()` - 获取系统毫秒时间戳
- `boot_port_flash_erase(addr, size)` - Flash 擦除，需自行处理扇区对齐
- `boot_port_flash_write(addr, data, len)` - Flash 写入（addr 4字节对齐，len 为4的倍数）
- `boot_port_flash_read(addr, data, len)` - Flash 读取
- `boot_port_uart_read(buf, max_len)` - 串口读取（非阻塞），返回实际读取字节数
- `boot_port_uart_write(data, len)` - 串口发送（阻塞）
- `boot_port_jump_to_app(app_addr)` - 跳转到 APP
- `boot_port_system_reset()` - 系统复位
- `boot_port_log(fmt, ...)` - 调试日志（可选）

### APP 端 (`boot_port_app.h`)

- `boot_port_app_flash_erase()` - Flash 擦除（标志位区）
- `boot_port_app_flash_write()` - Flash 写入
- `boot_port_app_flash_read()` - Flash 读取
- `boot_port_app_uart_read()` - 串口读取（非阻塞）
- `boot_port_app_uart_write()` - 串口发送（阻塞）
- `boot_port_app_system_reset()` - 系统复位
- `boot_port_app_log()` - 调试日志（可选）

## 任务调度器

Bootloader 和 APP 工程均使用简单的时间片轮询调度器（`scheduler.c`）：

```c
// Bootloader 端 scheduler_task 数组
{uart1_task, 100, 0},           // 串口任务，100ms 周期
{easy_bootloader_run, 10, 0},   // Bootloader 轮询，10ms 周期

// APP 端 scheduler_task 数组
{uart1_task, 100, 0},           // 串口任务，100ms 周期
{easy_bootloader_app_run, 10, 0},  // APP 端升级检测，10ms 周期
```

**Bootloader 端初始化流程**: `scheduler_init()` → `myusart_init()` → `easy_bootloader_init()`

**APP 端初始化流程**: `scheduler_init()` → `myusart_init()` → `easy_bootloader_app_init()`

## 开发注意事项

1. **应用层代码不可修改**: `easy_bootloader.c` 和 `easy_bootloader_app.c` 是通用实现，移植时只修改 `boot_port_xxx.c`
2. **链接地址配置**: APP 工程的链接脚本起始地址必须与 `BOOT_APP_START_ADDR` 一致
3. **中断向量表**: APP 启动时需设置 `SCB->VTOR = APP_START_ADDR`
4. **跳转前复位**: 跳转到 APP 前必须关闭中断、复位外设、清除 SysTick
5. **串口接收**: 必须使用 DMA + 空闲中断 + 环形缓冲区处理高速数据流，缓冲区大小建议 >= 2 × 最大包长度
6. **日志开关**: 通过 `boot_config.h` 中的 `BOOT_CONFIG_ENABLE_LOG` 宏控制日志输出

## 已知问题

- `easy_bootloader_app.c`（组件库和示例工程中均有副本）对 date 字段存在重复写入逻辑，修复时需同步所有副本

## 上位机工具配置

| 参数 | STM32F4 默认值 | CH32V307 值 |
|------|---------------|-------------|
| 波特率 | 115200 | 115200 |
| APP 基址 | `0x08010000` | `0x00006000` |
| 包长 | 128/256/512/1024 | 1024 |

包长包含 11 字节帧开销，实际数据最大 1013 字节。

## 关键文件

| 路径 | 说明 |
|------|------|
| `easy_bootloader_compoents/` | Bootloader 端核心代码（修改后需同步） |
| `easy_bootloader_app_compoents/` | APP 端核心代码（修改后需同步） |
| `stm32f4_example/*/project/Compoents/` | STM32 工程组件（从组件库拷贝） |
| `stm32f4_example/*/project/Myapp/` | 用户代码（调度器、串口、BSP） |
| `ch32v307_example/*/Components/` | CH32 工程组件（从组件库拷贝） |
| `协议.md` | 通信协议详细文档 |
| `DESIGN.md` | 框架设计文档 |

## 修改约束

1. **优先改组件库**: 在 `easy_bootloader*_compoents/` 下修改，然后同步到示例工程
2. **避免改核心逻辑**: `easy_bootloader.c` / `easy_bootloader_app.c` 为通用实现，移植适配放在 `boot_port*.c/h`
3. **配置一致性**: 确保 `BOOT_PACKET_MAX_SIZE` 与上位机包长设置一致
4. **地址检查**: APP 工程链接地址必须与 `BOOT_APP_START_ADDR` 一致
5. **CH32 特殊处理**: 使用别名地址 `0x0000_xxxx`，Flash 擦除默认值为 `0xE339E339`（非 0xFFFFFFFF）

“There’s a file modification bug in Claude Code. The workaround is: always use complete absolute Windows paths
with drive letters and backslashes for ALL file operations. Apply this rule going forward, not just for this
file.”
