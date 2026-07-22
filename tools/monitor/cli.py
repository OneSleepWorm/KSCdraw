#!/usr/bin/env python3
"""cli.py — monitor 包的 argparse 入口 + main()。

路径锚定规则:
- `_SCRIPT_DIR` = `KSCOS/tools/`（本文件所在 `monitor/` 目录的上一层）
- 所有默认路径（日志、PID、.data 目录）均相对 `_SCRIPT_DIR`，与 CWD 无关
"""
from __future__ import annotations

import argparse
import os
import sys

# 本文件位于 monitor/cli.py，要拿 KSCOS/tools/ 作为锚点需上两层
_MONITOR_DIR = os.path.dirname(os.path.abspath(__file__))         # KSCOS/tools/monitor/
_SCRIPT_DIR = os.path.dirname(_MONITOR_DIR)                      # KSCOS/tools/

DEFAULT_TRANSPORT = "file"
DEFAULT_PORT = "COM6"
DEFAULT_BAUD = 115200
DEFAULT_POLL_INTERVAL_MS = 50
DEFAULT_TCP_PORT = 12345
DEFAULT_IDLE = 60
DEFAULT_LOG = os.path.join(_SCRIPT_DIR, "logs", "serial.log")
DEFAULT_PID_FILE = os.path.join(_SCRIPT_DIR, "logs", "daemon.pid")
_FILE_CMD_PATH = os.path.join(_SCRIPT_DIR, "..", ".data", "stdin.txt")
_FILE_OUT_PATH = os.path.join(_SCRIPT_DIR, "..", ".data", "stdout.txt")


def _add_transport_args(p, include_data_paths: bool = False):
    """把 transport / 网络 / 日志 / PID 参数加到 parser.

    include_data_paths=True 时加 --cmd-path / --out-path（仅 daemon 子进程需要，
    client 不暴露，避免误用）。
    """
    p.add_argument("--transport", default=DEFAULT_TRANSPORT,
                   choices=["serial", "file", "mock"])
    p.add_argument("--port", default=DEFAULT_PORT)
    p.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    p.add_argument("--poll-interval", type=int, default=DEFAULT_POLL_INTERVAL_MS)
    p.add_argument("--tcp-port", type=int, default=DEFAULT_TCP_PORT)
    p.add_argument("--idle", type=int, default=DEFAULT_IDLE)
    p.add_argument("--exchange-poll", type=float, default=0.001,
                   help="exchange 轮询间隔（秒），默认 0.001")
    p.add_argument("--log", default=DEFAULT_LOG)
    p.add_argument("--pid-file", default=DEFAULT_PID_FILE)
    p.add_argument("--daemon", action="store_true", help=argparse.SUPPRESS)
    if include_data_paths:
        p.add_argument("--cmd-path", default=_FILE_CMD_PATH)
        p.add_argument("--out-path", default=_FILE_OUT_PATH)


def _build_parser() -> argparse.ArgumentParser:
    """构造完整 parser. 子命令通过 parents= 继承 transport args（修旧 Bug 3）。"""
    parent = argparse.ArgumentParser(add_help=False)
    _add_transport_args(parent, include_data_paths=True)

    ap = argparse.ArgumentParser(
        description="monitor.py — 多平台串口/文件传输工具",
        parents=[parent],  # 顶层也接受 transport args（兼容 `--transport X write`）
    )

    sub = ap.add_subparsers(dest="cmd")

    sub.add_parser("ping", parents=[parent])

    p_w = sub.add_parser("write", parents=[parent])
    p_w.add_argument("data", nargs="+")
    p_w.add_argument("--hex", action="store_true")
    p_w.add_argument("--noeol", action="store_true")

    p_ex = sub.add_parser("exchange", parents=[parent])
    p_ex.add_argument("data", nargs="+")
    p_ex.add_argument("--hex", action="store_true")
    p_ex.add_argument("--noeol", action="store_true")
    p_ex.add_argument("--expect", default=None,
                       help="等待回应中出现的子串；省略则收到数据静默 200ms 后返回")
    p_ex.add_argument("--timeout", type=float, default=3.0)

    p_mon = sub.add_parser("monitor", parents=[parent])
    p_mon.add_argument("--hex", action="store_true")
    p_mon.add_argument("--timeout", type=float, default=0,
                       help="exit after N seconds (0 = run until Ctrl+C)")

    sub.add_parser("start", parents=[parent], aliases=["open"])
    sub.add_parser("stop", parents=[parent], aliases=["close"])

    return ap


def main():
    """CLI 入口，daemon 子进程也走这里识别 --daemon。"""
    ap = _build_parser()

    if "--daemon" in sys.argv:
        # daemon 子进程：交给 monitor_daemon.Daemon
        args = ap.parse_args([a for a in sys.argv[1:] if a != "--daemon"])
        # sibling import：__main__.py 已把本目录加到 sys.path
        from daemon import run_daemon
        run_daemon(args)
        return

    args = ap.parse_args()
    if args.cmd is None:
        ap.print_help()
        sys.exit(1)

    from client import _client_run
    _client_run(args)


if __name__ == "__main__":
    main()