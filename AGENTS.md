# Repository Guidelines

## 仓库概况
- 组件库：`easy_bootloader_compoents/`（Bootloader）与 `easy_bootloader_app_compoents/`（APP），内含 `inc/`、`src/`，供 STM32F407 使用；示例工程下的 `Compoents/` 是拷贝版，修改需手工同步。
- 示例：`stm32f4_example/`（Keil，Bootloader 与 APP 双工程）。
- 工具与文档：`serial_terminal.py`（Tk UI 上位机）、`README.md`、`DESIGN.md`、`协议.md`、`CLAUDE.md`。
- 大量生成物在 `project/MDK-ARM/project/`、`obj/` 等目录，勿手改。

## 构建与烧录
- STM32F4：用 Keil 打开 `stm32f4_example/easy_bootloader_boot/project/MDK-ARM/project.uvprojx` 与 `.../easy_bootloader_app/project/MDK-ARM/project.uvprojx`，链接脚本起始地址必须匹配 `boot_config.h` 的 `BOOT_APP_START_ADDR`（默认 0x08010000）。
- 上位机：`pip install pyserial && python serial_terminal.py`，默认 115200。包长选 128/256/512/1024（含 11 字节帧开销），默认 APP 基址 `0x08010000`。

## 配置与地址
- 公共配置：`boot_config.h`（Boot）/`boot_config_app.h`（APP）定义区间和缓冲；Flash 写入 4 字节对齐。标志区布局固定：0x00 flag，0x04 version，0x08 date。
- STM32 默认：Bootloader 0x08000000+64KB，APP 0x08010000 最大 0xD0000，Flag 区 0x080E0000+128KB；SRAM 0x20000000-0x20030000，可选 CCM 0x10000000-0x10010000；`BOOT_PACKET_MAX_SIZE` 1013。
- APP 侧配置与标志区一致，宏名带 `_APP_` 前缀。

## 协议与流程
- 数据帧：`55 AA` + 剩余 3B（大端）+ 长度 2B（<=1013）+ 数据 + 校验 2B（长度起逐字节累加）+ `55 55`，ACK 固定 `55 AA FF FE 55 55`。
- APP 命令：查询版本 `55 AA FF DD 55 55` → 返回 `version:xx\r\n`；查询日期 `55 AA FF CC 55 55` → `YYYY-MM-DD\r\n`；触发升级 `55 AA [ver4B][date4B] FF EE 55 55`，版本不同则写 flag=1/版本/日期后复位。
- Boot 流程：上电读 flag；flag=1 留在 Boot；flag=2 且 APP 有效跳转；flag=擦除值则仅在 APP 无效时停留。下载阶段首次擦除 APP 区，4 字节对齐写入，最后一包 flush 后写 flag=2+version/date、回 ACK 并系统复位。

## 示例工程要点
- STM32 示例：UART1 打印、UART2 传输，DMA+空闲中断+RT-Thread ringbuffer；调度器周期执行 `easy_bootloader_run`/`easy_bootloader_app_run`；组件代码与根目录一致并附带 `ringbuffer.c/h`。
- 生成的 `.o/.d/.map/.hex/.elf` 保留观察，不要编辑。

## 修改约束
- 优先改 `easy_bootloader*_compoents/` 下源码；如同步示例工程，需覆盖 `stm32f4_example/.../Compoents/`。
- 避免随意更动 `easy_bootloader.c`/`easy_bootloader_app.c` 核心逻辑；板级适配放在 `boot_port_stm32f407.c` 或 `boot_port_app_stm32f407.c`。确保 `BOOT_PACKET_MAX_SIZE` 与上位机一致，Flash 擦写区间不交叉。
- 链接脚本/启动文件/驱动为生成物，非必要勿改。

## 验证清单
- Boot 与 APP 工程均能无警告编译并烧录；APP 链接地址与配置一致。
- 上位机发送一帧示例包，设备返回 ACK `55 AA FF FE 55 55`；触发升级命令后，Flag 区值更新且复位跳转正常。
- 串口收发保持 DMA/中断 + 环形缓冲无阻塞；必要时提供日志/示波截图。

## 额外注意
- `easy_bootloader_app.c`（两处拷贝）当前对 date 字段存在重复写入逻辑，修复需同步所有副本。
