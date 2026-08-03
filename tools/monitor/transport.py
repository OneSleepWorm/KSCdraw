#!/usr/bin/env python3
"""transport.py — 传输层抽象 + FileTransport / SerialTransport / MockTransport。

设计要点:
- Transport.implements_async_read 决定 daemon 是否启动 reader 线程。
  FileTransport (False) 的 IO 是请求-响应语义，daemon 在 exchange/monitor
  调点里直接 poll，不需要后台线程。SerialTransport (True) 需要后台线程
  捕获 MCU 自发数据（如 reset 后的 boot 日志）。

- FileTransport 启动时不清空 stdout.txt，而是把读偏移对到当前文件尾，
  这样 KSCOS 先/后启动都能保留 boot 输出。stdin.txt 仍清空（避免吃上次
  会话留的脏命令）。彻底清空两个文件由 monitor_daemon.Daemon 在 stop
  路径上统一负责（FileTransport.clear_data()）。

- MockTransport 用于单元测试，内存里玩。完全无 IO、无 socket、无文件。
"""
from __future__ import annotations

import os
import threading
from abc import ABC, abstractmethod
from typing import Optional


class Transport(ABC):
    """传输层接口。所有方法需线程安全。"""
    poll_interval: float = 0.05  # reader 线程无数据时 sleep 间隔
    implements_async_read: bool = False  # daemon 是否启动后台 reader 线程

    @abstractmethod
    def read_available(self) -> bytes:
        """非阻塞读所有可读字节。无数据返回 b''。"""

    @abstractmethod
    def write(self, data: bytes) -> int:
        """发送字节，返回实际写入字节数。"""

    @abstractmethod
    def close(self) -> None:
        """释放资源。"""

    def supports_clear(self) -> bool:
        """是否提供 clear_data() 能力（仅 file 模式 True）。"""
        return False

    def clear_data(self) -> None:
        """清空底层存储。默认不实现。"""

    def set_paused(self, paused: bool) -> None:
        """暂停/恢复后台 reader 消费底层数据。

        仅 async transport（如串口）需要。默认 no-op：file/mock 模式
        的 read_available 是请求-响应语义，daemon 没有后台 reader。
        """

    def is_paused(self) -> bool:
        return False


def make_transport(transport_type: str, **kwargs) -> "Transport":
    """工厂函数。"""
    if transport_type == "serial":
        return SerialTransport(kwargs["port"], kwargs["baud"])
    if transport_type == "file":
        return FileTransport(
            cmd_path=kwargs["cmd_path"],
            out_path=kwargs["out_path"],
            poll_interval_ms=kwargs.get("poll_interval_ms", 50),
        )
    if transport_type == "mock":
        return MockTransport()
    raise ValueError(f"unknown transport: {transport_type}")


# ── 文件后端 (PC) ────────────────────────────────────────

class FileTransport(Transport):
    """文件 IO 后端。

    命令文件 (cmd_path):  daemon append-write，KSCOS read+truncate
    输出文件 (out_path):  KSCOS append-write，daemon read-from-offset

    路径由调用方传入（client/daemon 共享 cli._FILE_CMD_PATH 等常量），
    不再依赖 CWD，避免多进程路径漂移。
    """
    implements_async_read = False

    def __init__(self, cmd_path: str, out_path: str,
                 poll_interval_ms: int = 50):
        self.cmd_path = cmd_path
        self.out_path = out_path
        self.poll_interval = poll_interval_ms / 1000.0
        self._out_fp: Optional[object] = None
        self._out_offset = 0
        self._lock = threading.Lock()
        self._open_files()

    def _open_files(self):
        """启动初始化。

        cmd_path: 清空（避免吃上次留下的脏命令）。
        out_path: 不清空，把读偏移对到当前文件尾。这样无论 KSCOS 先启动
        还是 daemon 先启动，都能保留 boot 输出，daemon 只读 daemon
        启动后 KSCOS 写入的新数据。
        """
        os.makedirs(os.path.dirname(self.cmd_path) or ".", exist_ok=True)
        os.makedirs(os.path.dirname(self.out_path) or ".", exist_ok=True)
        open(self.cmd_path, "wb").close()
        if not os.path.exists(self.out_path):
            open(self.out_path, "wb").close()
            self._out_offset = 0
        else:
            with open(self.out_path, "rb") as probe:
                probe.seek(0, os.SEEK_END)
                self._out_offset = probe.tell()
        self._out_fp = open(self.out_path, "rb")

    def read_available(self) -> bytes:
        with self._lock:
            if not self._out_fp:
                return b""
            self._out_fp.seek(0, os.SEEK_END)
            size = self._out_fp.tell()
            if size <= self._out_offset:
                return b""
            self._out_fp.seek(self._out_offset, os.SEEK_SET)
            data = self._out_fp.read(size - self._out_offset)
            self._out_offset = size
            return data or b""

    def write(self, data: bytes) -> int:
        with self._lock:
            with open(self.cmd_path, "ab") as f:
                f.write(data)
                f.flush()
            return len(data)

    def supports_clear(self) -> bool:
        return True

    def clear_data(self) -> None:
        """彻底清空 cmd_path + out_path + 重置 offset。由 daemon stop 调用。"""
        with self._lock:
            if self._out_fp:
                self._out_fp.close()
                self._out_fp = None
            open(self.cmd_path, "wb").close()
            open(self.out_path, "wb").close()
            self._out_offset = 0
            self._out_fp = open(self.out_path, "rb")

    def close(self):
        with self._lock:
            if self._out_fp:
                self._out_fp.close()
                self._out_fp = None


# ── 串口后端 (STM32) ────────────────────────────────────

class SerialTransport(Transport):
    """pyserial 后端。"""
    implements_async_read = True

    def __init__(self, port: str, baud: int):
        import serial as _serial  # 延迟导入：file 模式 / 测试不需要 pyserial
        self.ser = _serial.Serial(port, baud, timeout=0.05)
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()
        self._lock = threading.Lock()
        self._paused = False
        self.port = port
        self.baud = baud

    def read_available(self) -> bytes:
        with self._lock:
            n = self.ser.in_waiting
            return self.ser.read(n) if n else self.ser.read(1)

    def read_if_active(self) -> bytes:
        """后台 reader 专用读取。

        与 set_paused 在同一把锁内检查 paused + 读串口，保证 exchange/
        monitor 活跃期间 reader 原子性让路，不抢数据；也不会阻塞消费方的
        read_available()。
        """
        with self._lock:
            if self._paused:
                return b""
            n = self.ser.in_waiting
            return self.ser.read(n) if n else self.ser.read(1)

    def set_paused(self, paused: bool) -> None:
        with self._lock:
            self._paused = paused

    def is_paused(self) -> bool:
        with self._lock:
            return self._paused

    def write(self, data: bytes) -> int:
        with self._lock:
            return self.ser.write(data)

    def close(self):
        with self._lock:
            if self.ser and self.ser.is_open:
                self.ser.close()
                self.ser = None


# ── Mock 后端 (测试用) ──────────────────────────────────

class MockTransport(Transport):
    """内存 transport 用于单测。线程安全。

    - write() 把数据存到 outbox（模拟"发到下位机"的字节）
    - inject() 由测试代码注入字节到 inbox（模拟"下位机发回来"）
    - read_available() 返回 inbox 中所有当前字节并清空
    - close() 是 no-op
    """
    implements_async_read = False  # 测试环境下不需要 reader 线程（避免与 exchange 抢 inbox）

    def __init__(self):
        self._lock = threading.Lock()
        self._inbox = bytearray()
        self._outbox = bytearray()

    def read_available(self) -> bytes:
        with self._lock:
            if not self._inbox:
                return b""
            data = bytes(self._inbox)
            self._inbox.clear()
            return data

    def write(self, data: bytes) -> int:
        with self._lock:
            self._outbox.extend(data)
            return len(data)

    def inject(self, data: bytes):
        """测试用：模拟下位机往 inbox 写数据。"""
        with self._lock:
            self._inbox.extend(data)

    @property
    def outbox(self) -> bytes:
        with self._lock:
            return bytes(self._outbox)

    def reset_outbox(self):
        with self._lock:
            self._outbox.clear()

    def close(self):
        pass