#!/usr/bin/env python3
"""monitor package — 多平台串口/文件传输工具。

入口: `python KSCOS/tools/monitor <cmd> ...`
或等价: `python -m monitor <cmd> ...`（MONITOR_DIR 在 sys.path 时）

模块:
- cli:        argparse + main()
- transport:  Transport / FileTransport / SerialTransport / MockTransport
- daemon:     Daemon 类 + log_entry + PID 助手 + 子进程入口
- client:     _send_to_daemon + _ensure_daemon + _client_run
"""
from .cli import main

__all__ = ["main"]