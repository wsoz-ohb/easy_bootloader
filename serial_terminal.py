#!/usr/bin/env python3
"""
简单串口上位机
----------------
功能特性：
1. 自动枚举可用串口，支持刷新。
2. 配置串口参数（波特率 / 数据位 / 校验位 / 停止位）。
3. 打开 / 关闭串口，实时显示接收数据，可选十六进制格式。
4. 文本/十六进制发送窗口，支持回车发送、清空/保存日志。

运行要求：
- Python 3.8+
- pyserial (`pip install pyserial`)

后续可以在此基础上集成 Bootloader 协议。
"""

from __future__ import annotations

import queue
import sys
import threading
import time
from pathlib import Path
from typing import Callable, Optional

import serial
import serial.tools.list_ports
import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, ttk


BAUD_RATES = (
    "9600",
    "19200",
    "38400",
    "57600",
    "74880",
    "115200",
    "128000",
    "230400",
    "256000",
    "460800",
    "921600",
)

DATA_BITS = {
    "5": serial.FIVEBITS,
    "6": serial.SIXBITS,
    "7": serial.SEVENBITS,
    "8": serial.EIGHTBITS,
}

PARITIES = {
    "None": serial.PARITY_NONE,
    "Even": serial.PARITY_EVEN,
    "Odd": serial.PARITY_ODD,
    "Mark": serial.PARITY_MARK,
    "Space": serial.PARITY_SPACE,
}

STOP_BITS = {
    "1": serial.STOPBITS_ONE,
    "1.5": serial.STOPBITS_ONE_POINT_FIVE,
    "2": serial.STOPBITS_TWO,
}

# Bootloader 最大包大小选项
PACKET_SIZES = ("128", "256", "512", "1024")

# APP 端命令帧
CMD_QUERY_VERSION = bytes([0x55, 0xAA, 0xFF, 0xDD, 0x55, 0x55])
CMD_QUERY_DATE = bytes([0x55, 0xAA, 0xFF, 0xCC, 0x55, 0x55])
CMD_START_FLASH = bytes([0x55, 0xAA, 0xFF, 0xEE, 0x55, 0x55])  # 简化版触发升级命令

# Bootloader 帧固定开销: 2B 头 + 3B 剩余 + 2B 长度 + 2B 校验 + 2B 尾
BOOT_FRAME_OVERHEAD = 11
# 首包 ACK 等待时间（用于应对长时间擦除），后续包维持较短超时
ACK_TIMEOUT_FIRST = 10.0
ACK_TIMEOUT_OTHERS = 5.0


def build_finish_frame(version: int, date: int) -> bytes:
    """构建完成帧: 55 AA [ver 4B] [date 4B] FF FD 55 55"""
    return bytes([
        0x55, 0xAA,
        (version >> 24) & 0xFF, (version >> 16) & 0xFF,
        (version >> 8) & 0xFF, version & 0xFF,
        (date >> 24) & 0xFF, (date >> 16) & 0xFF,
        (date >> 8) & 0xFF, date & 0xFF,
        0xFF, 0xFD, 0x55, 0x55
    ])


def hex_string(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def parse_hex_string(text: str) -> bytes:
    cleaned = text.strip().replace(",", " ")
    if not cleaned:
        return b""
    try:
        return bytes(int(tok, 16) for tok in cleaned.split())
    except ValueError as exc:
        raise ValueError("十六进制内容格式错误（示例: 01 0A FF）") from exc


class SerialWorker:
    """负责串口打开/关闭及后台读取。"""

    def __init__(self) -> None:
        self.serial: Optional[serial.Serial] = None
        self._reader: Optional[threading.Thread] = None
        self._stop_evt = threading.Event()
        self._listeners: list[Callable[[bytes], None]] = []

    def open(
        self,
        port: str,
        baudrate: int,
        bytesize: int,
        parity: str,
        stopbits: float,
        listener: Optional[Callable[[bytes], None]] = None,
    ) -> None:
        self.close()
        try:
            self.serial = serial.Serial(
                port=port,
                baudrate=baudrate,
                bytesize=bytesize,
                parity=parity,
                stopbits=stopbits,
                timeout=0.1,
            )
        except serial.SerialException as exc:
            raise RuntimeError(f"打开串口失败：{exc}") from exc
        self._listeners = []
        if listener:
            self.add_listener(listener)
        self._stop_evt.clear()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def close(self) -> None:
        self._stop_evt.set()
        if self._reader and self._reader.is_alive():
            self._reader.join(timeout=0.5)
        self._reader = None
        if self.serial:
            try:
                self.serial.close()
            except serial.SerialException:
                pass
        self.serial = None
        self._listeners = []

    def add_listener(self, listener: Callable[[bytes], None]) -> None:
        if listener not in self._listeners:
            self._listeners.append(listener)

    def remove_listener(self, listener: Callable[[bytes], None]) -> None:
        if listener in self._listeners:
            self._listeners.remove(listener)

    def write(self, data: bytes) -> None:
        if not self.serial or not self.serial.is_open:
            raise RuntimeError("串口未打开")
        try:
            self.serial.write(data)
        except serial.SerialException as exc:
            raise RuntimeError(f"发送失败：{exc}") from exc

    def _read_loop(self) -> None:
        assert self.serial is not None
        while not self._stop_evt.is_set():
            try:
                data = self.serial.read(self.serial.in_waiting or 1)
            except serial.SerialException:
                break
            if not data:
                continue
            for listener in list(self._listeners):
                try:
                    listener(data)
                except Exception:
                    continue
        self._stop_evt.clear()


class BootloaderUploader:
    """负责固件（HEX/BIN）分包、发送以及 ACK 检测。"""

    ACK_PATTERN = bytes([0x55, 0xAA, 0xFF, 0xFE, 0x55, 0x55])
    APP_BASE_ADDR = 0x08010000  # 默认 STM32F4 基址，可在 UI 中修改

    def __init__(
        self,
        worker: SerialWorker,
        logger: Callable[[str], None],
        status_cb: Callable[[str], None],
        app_base_addr: int | None = None,
        finish_cb: Optional[Callable[[bool], None]] = None,
    ) -> None:
        self.worker = worker
        self.logger = logger
        self.status_cb = status_cb
        self.finish_cb = finish_cb
        self.frame_overhead = BOOT_FRAME_OVERHEAD
        self.app_base_addr = app_base_addr or self.APP_BASE_ADDR
        self.file_path: Optional[Path] = None
        # max_payload 表示纯数据长度，UI 里配置的是"整包长度"，需扣掉帧固定开销
        self.max_payload = 1024 - self.frame_overhead
        self._upload_thread: Optional[threading.Thread] = None
        self._ack_event = threading.Event()
        self._ack_buffer = bytearray()
        self._listener_registered = False
        self._lock = threading.Lock()
        # 版本号和日期，用于完成帧
        self.version: int = 1
        self.date: int = 0

    def is_running(self) -> bool:
        return bool(self._upload_thread and self._upload_thread.is_alive())

    def set_max_payload(self, total_frame_size: int) -> None:
        """设置最大包大小（UI 配置的是整包长度，内部换算为纯 payload）"""
        if total_frame_size <= self.frame_overhead:
            self.logger(f"包长度至少需要 {self.frame_overhead + 1} 字节（含固定头尾）")
            return
        payload_size = total_frame_size - self.frame_overhead
        self.max_payload = payload_size
        self.logger(f"已设置整包长度 {total_frame_size} 字节，数据区 {payload_size} 字节")

    def set_file(self, path: Path) -> None:
        self.file_path = path
        self.status_cb(str(path))

    def start(self) -> None:
        if self.is_running():
            self.logger("刷写任务正在进行，请稍候...")
            return
        if not self.file_path:
            self.logger("请先选择 HEX 文件")
            return
        self.logger(f"准备刷写: {self.file_path}")
        self._upload_thread = threading.Thread(target=self._run_upload, daemon=True)
        self._upload_thread.start()

    def _run_upload(self) -> None:
        if not self.worker.serial:
            self.logger("串口未打开，无法刷写")
            self._notify_finish(False)
            return
        data, base_addr = self._load_firmware()
        if data is None:
            self.logger("文件为空，已取消")
            self._notify_finish(False)
            return
        # 基地址一致性提示，但允许用户自选基址继续
        if base_addr is not None and base_addr != self.app_base_addr:
            self.logger(
                f"提示：HEX 基地址 0x{base_addr:08X} 与当前 APP 起始 0x{self.app_base_addr:08X} 不一致，已中止刷写。"
            )
            self._unregister_listener()
            self._notify_finish(False)
            return

        total = len(data)
        self.logger(f"开始刷写：{self.file_path.name} ({total} 字节)")
        self._register_listener()

        offset = 0
        success = True
        try:
            # 阶段1：发送所有数据帧
            while offset < total:
                chunk = data[offset : offset + self.max_payload]
                offset += len(chunk)
                remaining = total - offset
                frame = self._build_frame(chunk, remaining)
                self._ack_event.clear()
                try:
                    self.worker.write(frame)
                except RuntimeError as exc:
                    self.logger(f"发送失败：{exc}")
                    success = False
                    break

                # 首包可能耗时长（擦除 Flash），拉长超时窗口
                ack_timeout = ACK_TIMEOUT_FIRST if offset == len(chunk) else ACK_TIMEOUT_OTHERS
                if not self._ack_event.wait(timeout=ack_timeout):
                    self.logger("等待 ACK 超时，刷写中断")
                    success = False
                    break

                percent = offset * 100 // total
                self.status_cb(f"已发送 {offset}/{total} 字节 ({percent}%)")

            # 阶段2：发送完成帧
            if success and offset == total:
                self.logger("数据发送完成，发送完成帧...")
                self.status_cb("发送完成帧...")

                finish_frame = build_finish_frame(self.version, self.date)
                self._ack_event.clear()
                try:
                    self.worker.write(finish_frame)
                    self.logger(f"完成帧: ver={self.version}, date=0x{self.date:08X}")
                except RuntimeError as exc:
                    self.logger(f"发送完成帧失败：{exc}")
                    success = False

                if success:
                    if not self._ack_event.wait(timeout=ACK_TIMEOUT_OTHERS):
                        self.logger("等待完成帧 ACK 超时")
                        success = False

        finally:
            self._unregister_listener()

        final_ok = success and offset == total
        if final_ok:
            self.logger("升级完成，设备即将重启运行新固件")
            self.status_cb("升级完成")
        elif not success:
            self.logger("刷写失败")
        self._notify_finish(final_ok)

    def _register_listener(self) -> None:
        with self._lock:
            if not self._listener_registered:
                self.worker.add_listener(self._on_serial_data)
                self._listener_registered = True

    def _unregister_listener(self) -> None:
        with self._lock:
            if self._listener_registered:
                self.worker.remove_listener(self._on_serial_data)
                self._listener_registered = False
        self._ack_buffer.clear()
        self._ack_event.clear()

    def _on_serial_data(self, data: bytes) -> None:
        self._ack_buffer.extend(data)
        ack = self.ACK_PATTERN
        while True:
            idx = self._ack_buffer.find(ack)
            if idx == -1:
                break
            # 移除已匹配的 ACK 之前部分
            del self._ack_buffer[: idx + len(ack)]
            self._ack_event.set()

    @staticmethod
    def _build_frame(payload: bytes, remaining: int) -> bytes:
        if remaining < 0 or remaining > 0xFFFFFF:
            raise ValueError("剩余字节数超出 24 位范围")
        header = bytes([0x55, 0xAA])
        remaining_bytes = remaining.to_bytes(3, "big")
        length_bytes = len(payload).to_bytes(2, "big")
        checksum = BootloaderUploader._calc_checksum(length_bytes + payload)
        tail = bytes([0x55, 0x55])
        return b"".join([header, remaining_bytes, length_bytes, payload, checksum, tail])

    @staticmethod
    def _calc_checksum(data: bytes) -> bytes:
        crc = 0
        for byte in data:
            crc = (crc + byte) & 0xFFFF
        return crc.to_bytes(2, "big")

    def _load_firmware(self) -> tuple[Optional[bytes], Optional[int]]:
        if not self.file_path:
            return (None, None)
        try:
            suffix = self.file_path.suffix.lower()
            if suffix == ".hex":
                image, base_addr = self._load_intel_hex(self.file_path)
                return (image, base_addr)
            data = self.file_path.read_bytes()
            return (data if data else None, None)
        except (OSError, ValueError) as exc:
            self.logger(f"读取固件失败：{exc}")
            return (None, None)

    def _notify_finish(self, ok: bool) -> None:
        if self.finish_cb:
            try:
                self.finish_cb(ok)
            except Exception:
                pass

    def _load_intel_hex(self, path: Path) -> tuple[Optional[bytes], Optional[int]]:
        upper_addr = 0
        data_map: dict[int, int] = {}
        try:
            lines = path.read_text().splitlines()
        except UnicodeDecodeError:
            raise ValueError("HEX 文件编码错误")
        base_addr: Optional[int] = None
        for idx, raw_line in enumerate(lines, 1):
            line = raw_line.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise ValueError(f"第 {idx} 行不是合法的 Intel HEX 行")
            try:
                byte_count = int(line[1:3], 16)
                offset = int(line[3:7], 16)
                record_type = int(line[7:9], 16)
            except ValueError:
                raise ValueError(f"第 {idx} 行头部格式错误")
            data_str = line[9 : 9 + byte_count * 2]
            checksum = int(line[9 + byte_count * 2 : 11 + byte_count * 2], 16)
            calc = byte_count + (offset >> 8) + (offset & 0xFF) + record_type
            payload = []
            for i in range(0, len(data_str), 2):
                byte = int(data_str[i : i + 2], 16)
                payload.append(byte)
                calc += byte
            calc = ((calc ^ 0xFF) + 1) & 0xFF
            if calc != checksum:
                raise ValueError(f"第 {idx} 行校验和错误")

            if record_type == 0x00:  # data
                abs_addr = upper_addr + offset
                if base_addr is None:
                    base_addr = abs_addr
                for i, val in enumerate(payload):
                    data_map[abs_addr + i] = val
            elif record_type == 0x01:  # EOF
                break
            elif record_type == 0x04:  # extended linear
                if byte_count != 2:
                    raise ValueError(f"第 {idx} 行扩展地址长度错误")
                upper_addr = int("".join(f"{b:02X}" for b in payload), 16) << 16
            else:
                # 忽略其他记录类型
                continue

        if not data_map:
            return (None, None)

        min_addr = min(data_map)
        max_addr = max(data_map)
        length = max_addr - min_addr + 1
        image = bytearray([0xFF] * length)
        for addr, val in sorted(data_map.items()):
            image[addr - min_addr] = val
        return (bytes(image), min_addr)


class SerialTerminal(tk.Tk):
    """Tkinter UI 主窗体。"""

    def __init__(self) -> None:
        super().__init__()
        self.title("Easy Bootloader 串口终端 v2.0.1")
        self.geometry("1150x760")
        self.minsize(1020, 660)

        self.worker = SerialWorker()
        self.rx_queue: "queue.Queue[bytes]" = queue.Queue()
        self.hex_display = tk.BooleanVar(value=False)
        self.hex_send = tk.BooleanVar(value=False)
        self.auto_scroll = tk.BooleanVar(value=True)
        self.append_newline = tk.BooleanVar(value=False)
        self.bin_path_var = tk.StringVar(value="")
        self.boot_status_var = tk.StringVar(value="未选择文件")
        self.packet_size_var = tk.StringVar(value="1024")
        self.app_base_var = tk.StringVar(value=f"0x{BootloaderUploader.APP_BASE_ADDR:08X}")
        self.new_version_var = tk.StringVar(value="1")

        self.bootloader = BootloaderUploader(
            self.worker,
            self._log_async,
            self._update_boot_status,
            app_base_addr=None,
            finish_cb=self._on_upload_finished,
        )

        self._build_widgets()
        self._refresh_ports()
        self.after(100, self._process_rx_queue)

    # ----------------- UI 构建 -----------------
    def _build_widgets(self) -> None:
        paned = ttk.Panedwindow(self, orient=tk.HORIZONTAL)
        paned.pack(fill="both", expand=True, padx=10, pady=10)

        settings_frame = ttk.LabelFrame(paned, text="连接配置")
        paned.add(settings_frame, weight=0)

        ttk.Label(settings_frame, text="串口").grid(row=0, column=0, sticky="w", padx=8, pady=(12, 4))
        self.port_combo = ttk.Combobox(settings_frame, state="readonly", width=18)
        self.port_combo.grid(row=1, column=0, padx=8, sticky="we")

        ttk.Button(settings_frame, text="刷新列表", command=self._refresh_ports).grid(
            row=2, column=0, padx=8, pady=4, sticky="we"
        )

        ttk.Separator(settings_frame, orient=tk.HORIZONTAL).grid(row=3, column=0, sticky="we", pady=8)

        ttk.Label(settings_frame, text="波特率").grid(row=4, column=0, sticky="w", padx=8)
        self.baud_combo = ttk.Combobox(settings_frame, values=BAUD_RATES, width=18)
        self.baud_combo.set("115200")
        self.baud_combo.grid(row=5, column=0, padx=8, pady=2, sticky="we")

        # 数据位/校验位/停止位 压缩为一行
        extra_frame = ttk.Frame(settings_frame)
        extra_frame.grid(row=6, column=0, padx=8, pady=(8, 2), sticky="we")

        ttk.Label(extra_frame, text="数据位").pack(side="left")
        self.data_bits_combo = ttk.Combobox(
            extra_frame, values=list(DATA_BITS.keys()), width=3, state="readonly"
        )
        self.data_bits_combo.set("8")
        self.data_bits_combo.pack(side="left", padx=(2, 8))

        ttk.Label(extra_frame, text="校验位").pack(side="left")
        self.parity_combo = ttk.Combobox(
            extra_frame, values=list(PARITIES.keys()), width=5, state="readonly"
        )
        self.parity_combo.set("None")
        self.parity_combo.pack(side="left", padx=(2, 8))

        ttk.Label(extra_frame, text="停止").pack(side="left")
        self.stop_bits_combo = ttk.Combobox(
            extra_frame, values=list(STOP_BITS.keys()), width=3, state="readonly"
        )
        self.stop_bits_combo.set("1")
        self.stop_bits_combo.pack(side="left", padx=(2, 0))

        self.connect_btn = ttk.Button(
            settings_frame, text="打开串口", command=self._toggle_connection
        )
        self.connect_btn.grid(row=7, column=0, padx=8, pady=(12, 4), sticky="we")

        send_ctrl_frame = ttk.LabelFrame(settings_frame, text="发送控制")
        send_ctrl_frame.grid(row=8, column=0, padx=8, pady=(8, 8), sticky="nsew")
        send_ctrl_frame.columnconfigure(0, weight=1)

        chk_frame = ttk.Frame(send_ctrl_frame)
        chk_frame.grid(row=0, column=0, sticky="w", padx=4, pady=(4, 2))
        ttk.Checkbutton(chk_frame, text="HEX发送", variable=self.hex_send).pack(side="left")
        ttk.Checkbutton(chk_frame, text="附加CR/LF", variable=self.append_newline).pack(side="left", padx=(8, 0))

        ttk.Button(send_ctrl_frame, text="发送", command=self._send_data).grid(
            row=1, column=0, sticky="we", padx=4, pady=(4, 4)
        )

        boot_frame = ttk.LabelFrame(settings_frame, text="Bootloader 刷写")
        boot_frame.grid(row=9, column=0, padx=8, pady=(0, 8), sticky="nsew")
        boot_frame.columnconfigure(0, weight=1)

        ttk.Entry(boot_frame, textvariable=self.bin_path_var).grid(
            row=0, column=0, padx=4, pady=(4, 2), sticky="we"
        )

        file_opt_frame = ttk.Frame(boot_frame)
        file_opt_frame.grid(row=1, column=0, padx=4, pady=2, sticky="we")
        ttk.Button(file_opt_frame, text="选择文件", command=self._select_bin_file).pack(side="left")
        ttk.Label(file_opt_frame, text="  包大小:").pack(side="left")
        self.packet_size_combo = ttk.Combobox(
            file_opt_frame,
            textvariable=self.packet_size_var,
            values=PACKET_SIZES,
            width=6,
            state="readonly",
        )
        self.packet_size_combo.pack(side="left", padx=(2, 0))

        base_frame = ttk.Frame(boot_frame)
        base_frame.grid(row=2, column=0, padx=4, pady=2, sticky="we")
        ttk.Label(base_frame, text="APP基址:").pack(side="left")
        self.app_base_entry = ttk.Entry(base_frame, textvariable=self.app_base_var, width=14)
        self.app_base_entry.pack(side="left", padx=(4, 0))
        ttk.Button(base_frame, text="应用", command=self._apply_app_base).pack(side="left", padx=(4, 0))

        # 版本号输入行（用于完成帧）
        ver_frame = ttk.Frame(boot_frame)
        ver_frame.grid(row=3, column=0, padx=4, pady=2, sticky="we")
        ttk.Label(ver_frame, text="新版本:").pack(side="left")
        ttk.Entry(ver_frame, textvariable=self.new_version_var, width=8).pack(side="left", padx=(4, 0))
        ttk.Label(ver_frame, text="(刷写完成后写入)", foreground="gray").pack(side="left", padx=(4, 0))

        self.flash_btn = ttk.Button(boot_frame, text="开始刷写", command=self._start_bin_upload)
        self.flash_btn.grid(row=4, column=0, padx=4, pady=2, sticky="we")
        ttk.Label(
            boot_frame, textvariable=self.boot_status_var, wraplength=180, foreground="gray"
        ).grid(row=5, column=0, padx=4, pady=(2, 4), sticky="w")

        # 快捷命令区域
        cmd_frame = ttk.LabelFrame(settings_frame, text="快捷命令")
        cmd_frame.grid(row=10, column=0, padx=8, pady=(0, 8), sticky="nsew")
        cmd_frame.columnconfigure(0, weight=1)
        cmd_frame.columnconfigure(1, weight=1)

        # 查询按钮放在一行
        ttk.Button(cmd_frame, text="查询版本", command=self._send_query_version).grid(
            row=0, column=0, padx=(4, 2), pady=(6, 4), sticky="we"
        )
        ttk.Button(cmd_frame, text="查询日期", command=self._send_query_date).grid(
            row=0, column=1, padx=(2, 4), pady=(6, 4), sticky="we"
        )

        # 触发升级按钮单独一行
        ttk.Button(cmd_frame, text="触发升级 (进入Bootloader模式)", command=self._send_start_flash).grid(
            row=1, column=0, columnspan=2, padx=4, pady=(4, 6), sticky="we"
        )

        for child in settings_frame.winfo_children():
            child.configure(takefocus=True)

        right_frame = ttk.Frame(paned)
        paned.add(right_frame, weight=1)

        io_paned = ttk.Panedwindow(right_frame, orient=tk.VERTICAL)
        io_paned.pack(fill="both", expand=True, padx=(10, 0), pady=(0, 10))

        display_frame = ttk.LabelFrame(io_paned, text="接收区")
        io_paned.add(display_frame, weight=7)

        options_frame = ttk.Frame(display_frame)
        options_frame.pack(anchor="w", pady=3, padx=5)
        ttk.Checkbutton(
            options_frame, text="十六进制显示", variable=self.hex_display
        ).pack(side="left")
        ttk.Checkbutton(
            options_frame, text="自动滚动", variable=self.auto_scroll
        ).pack(side="left", padx=(10, 0))

        btn_frame = ttk.Frame(display_frame)
        btn_frame.pack(anchor="e", pady=3, padx=5)
        ttk.Button(btn_frame, text="清空", command=self._clear_log).pack(side="left", padx=5)
        ttk.Button(btn_frame, text="保存日志", command=self._save_log).pack(side="left")

        self.rx_text = scrolledtext.ScrolledText(display_frame, wrap="word")
        self.rx_text.pack(fill="both", expand=True, padx=5, pady=(0, 5))
        self.rx_text.configure(state="disabled")

        send_frame = ttk.LabelFrame(io_paned, text="发送区")
        io_paned.add(send_frame, weight=3)

        self.send_entry = tk.Text(send_frame, height=7)
        self.send_entry.pack(fill="x", padx=5, pady=5)
        self.send_entry.bind("<Control-Return>", lambda event: self._send_data())
        self.send_entry.bind("<Shift-Return>", lambda event: self._send_data())

    def _select_bin_file(self) -> None:
        path = filedialog.askopenfilename(
            title="选择 HEX 文件",
            filetypes=[("Intel HEX", "*.hex"), ("All Files", "*.*")],
        )
        if not path:
            return
        self.bin_path_var.set(path)
        self.bootloader.set_file(Path(path))

    def _start_bin_upload(self) -> None:
        path = self.bin_path_var.get().strip()
        if path:
            self.bootloader.set_file(Path(path))
        if self.bootloader.is_running():
            self._log_async("刷写任务正在进行，请稍候...")
            return
        # 设置包大小
        try:
            packet_size = int(self.packet_size_var.get())
            self.bootloader.set_max_payload(packet_size)
        except ValueError:
            self.bootloader.set_max_payload(1024)
        # 设置基址
        try:
            base_str = self.app_base_var.get().strip().lower()
            base_val = int(base_str, 16) if base_str.startswith("0x") else int(base_str, 0)
            self.bootloader.app_base_addr = base_val
        except ValueError:
            self._log_async("APP 基址格式错误，例如 0x08006000")
            return
        # 设置版本号
        try:
            self.bootloader.version = int(self.new_version_var.get().strip())
        except ValueError:
            self.bootloader.version = 1
        # 设置日期（自动使用当前日期）
        now = time.localtime()
        self.bootloader.date = (now.tm_year << 16) | (now.tm_mon << 8) | now.tm_mday

        self.flash_btn.configure(state="disabled")
        self.bootloader.start()
        if not self.bootloader.is_running():
            self.flash_btn.configure(state="normal")

    def _apply_app_base(self) -> None:
        """读取 APP 基址输入框并更新 uploader 配置"""
        try:
            base_str = self.app_base_var.get().strip().lower()
            base_val = int(base_str, 16) if base_str.startswith("0x") else int(base_str, 0)
            self.bootloader.app_base_addr = base_val
            self.boot_status_var.set(f"基址已设为 0x{base_val:08X}")
        except ValueError:
            self._log_async("APP 基址格式错误，例如 0x08006000")
            self.boot_status_var.set("APP 基址格式错误")

    def _send_query_version(self) -> None:
        """发送查询版本命令"""
        if not self.worker.serial:
            messagebox.showwarning("提示", "请先打开串口")
            return
        try:
            self.worker.write(CMD_QUERY_VERSION)
            self._log_line(f"[TX] 查询版本: {hex_string(CMD_QUERY_VERSION)}", highlight=True)
        except RuntimeError as exc:
            messagebox.showerror("错误", str(exc))

    def _send_query_date(self) -> None:
        """发送查询日期命令"""
        if not self.worker.serial:
            messagebox.showwarning("提示", "请先打开串口")
            return
        try:
            self.worker.write(CMD_QUERY_DATE)
            self._log_line(f"[TX] 查询日期: {hex_string(CMD_QUERY_DATE)}", highlight=True)
        except RuntimeError as exc:
            messagebox.showerror("错误", str(exc))

    def _send_start_flash(self) -> None:
        """发送触发升级命令（简化版，不携带版本/日期）"""
        if not self.worker.serial:
            messagebox.showwarning("提示", "请先打开串口")
            return

        try:
            self.worker.write(CMD_START_FLASH)
            self._log_line(f"[TX] 触发升级: {hex_string(CMD_START_FLASH)}", highlight=True)
        except RuntimeError as exc:
            messagebox.showerror("错误", str(exc))

    def _handle_serial_bytes(self, data: bytes) -> None:
        self.rx_queue.put(data)

    def _log_async(self, text: str, highlight: bool = False) -> None:
        self.after(0, lambda: self._log_line(text, highlight))

    def _update_boot_status(self, text: str) -> None:
        self.after(0, lambda: self.boot_status_var.set(text))

    def _on_upload_finished(self, ok: bool) -> None:
        def _enable() -> None:
            self.flash_btn.configure(state="normal")
        self.after(0, _enable)

    # ----------------- 串口操作 -----------------
    def _refresh_ports(self) -> None:
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.port_combo.configure(values=ports)
        if ports:
            self.port_combo.set(ports[0])
        else:
            self.port_combo.set("")

    def _toggle_connection(self) -> None:
        if self.worker.serial:
            self.worker.close()
            self.connect_btn.configure(text="打开串口")
            self._log_line("已关闭串口", highlight=True)
            return

        port = self.port_combo.get().strip()
        if not port:
            messagebox.showwarning("提示", "请选择串口")
            return

        try:
            baud = int(self.baud_combo.get())
        except ValueError:
            messagebox.showerror("错误", "波特率格式错误")
            return

        try:
            self.worker.open(
                port=port,
                baudrate=baud,
                bytesize=DATA_BITS[self.data_bits_combo.get()],
                parity=PARITIES[self.parity_combo.get()],
                stopbits=STOP_BITS[self.stop_bits_combo.get()],
                listener=self._handle_serial_bytes,
            )
        except (KeyError, RuntimeError) as exc:
            messagebox.showerror("错误", str(exc))
            return

        self.connect_btn.configure(text="关闭串口")
        self._log_line(f"打开串口 {port} @ {baud}", highlight=True)

    def _send_data(self) -> None:
        text = self.send_entry.get("1.0", tk.END).strip()
        if not text:
            return
        if not self.worker.serial:
            messagebox.showwarning("提示", "请先打开串口")
            return

        append_crlf = self.append_newline.get()

        try:
            if self.hex_send.get():
                payload = parse_hex_string(text)
            else:
                payload = text.encode("utf-8")
        except ValueError as exc:
            messagebox.showerror("错误", str(exc))
            return

        data = payload + (b"\r\n" if append_crlf else b"")

        try:
            self.worker.write(data)
        except RuntimeError as exc:
            messagebox.showerror("错误", str(exc))
            return

        timestamp = time.strftime("%H:%M:%S")
        # 发送窗口仅显示RX数据，避免接收区噪声

    # ----------------- 日志处理 -----------------
    def _process_rx_queue(self) -> None:
        while True:
            try:
                data = self.rx_queue.get_nowait()
            except queue.Empty:
                break

            if self.hex_display.get():
                chunk = hex_string(data) + " "
            else:
                chunk = data.decode("utf-8", errors="replace")
            self._append_rx_text(chunk)

        self.after(100, self._process_rx_queue)

    def _log_line(self, text: str, highlight: bool = False) -> None:
        self.rx_text.configure(state="normal")
        if highlight:
            self.rx_text.insert(tk.END, f"{text}\n", ("highlight",))
        else:
            self.rx_text.insert(tk.END, f"{text}\n")
        if self.auto_scroll.get():
            self.rx_text.see(tk.END)
        self.rx_text.configure(state="disabled")
        self.rx_text.tag_config("highlight", foreground="blue")

    def _append_rx_text(self, text: str) -> None:
        """ASCII 显示模式下，直接追加串口内容，不强制换行，避免分包造成的断裂。"""
        if not text:
            return
        self.rx_text.configure(state="normal")
        self.rx_text.insert(tk.END, text)
        if self.auto_scroll.get():
            self.rx_text.see(tk.END)
        self.rx_text.configure(state="disabled")

    def _clear_log(self) -> None:
        self.rx_text.configure(state="normal")
        self.rx_text.delete("1.0", tk.END)
        self.rx_text.configure(state="disabled")

    def _save_log(self) -> None:
        content = self.rx_text.get("1.0", tk.END).strip()
        if not content:
            messagebox.showinfo("提示", "日志为空")
            return
        default_name = f"serial_log_{time.strftime('%Y%m%d_%H%M%S')}.txt"
        path = filedialog.asksaveasfilename(
            title="保存日志",
            defaultextension=".txt",
            initialfile=default_name,
            filetypes=[("Text Files", "*.txt"), ("All Files", "*.*")],
        )
        if not path:
            return
        try:
            Path(path).write_text(content, encoding="utf-8")
        except OSError as exc:
            messagebox.showerror("错误", f"保存失败：{exc}")

    # ----------------- 退出 -----------------
    def on_close(self) -> None:
        self.worker.close()
        self.destroy()


def main() -> None:
    app = SerialTerminal()
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
