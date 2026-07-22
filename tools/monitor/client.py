#!/usr/bin/env python3
"""client.py — 客户端逻辑。

职责:
- _send_to_daemon: 把一条 JSON 命令发给 daemon，拿回 JSON 响应。
  异常分支全 cover，空 resp 抛 RuntimeError 由调用方接。
- port_responds: ping 一次看 daemon 是否健康
- _ensure_daemon: 确保 daemon 在跑；没有就启动，僵尸就先杀再启
- _client_run: dispatch 各子命令 (ping/write/exchange/monitor)
- _safe_print: 任何异常 → JSON 错误
"""
from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import time
from typing import Any, Callable

from daemon import (
    read_pid, is_pid_alive, kill_pid, clear_pid,
)


# ── 与 daemon 通信 ──────────────────────────────────────

def _send_to_daemon(obj: dict, tcp_port: int, timeout: float = 3.0) -> dict:
    """发一条 JSON 命令给 daemon，返回 JSON 响应。

    永远不会抛 JSONDecodeError 给 caller：空 resp 转 RuntimeError，由
    _safe_print 接住转 JSON 错误。
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect(("127.0.0.1", tcp_port))
        sock.sendall(json.dumps(obj).encode("utf-8"))
    except (ConnectionRefusedError, ConnectionResetError, OSError):
        sock.close()
        raise

    resp = b""
    while True:
        try:
            chunk = sock.recv(4096)
        except (socket.timeout, ConnectionResetError, ConnectionAbortedError):
            break
        if not chunk:
            break
        resp += chunk
        if b"\n" in resp:
            break
    sock.close()

    if not resp:
        raise RuntimeError("no response from daemon")
    return json.loads(resp.split(b"\n", 1)[0].decode("utf-8"))


def port_responds(tcp_port: int) -> bool:
    """快速 ping 一次 daemon 看是否健康。"""
    try:
        r = _send_to_daemon({"cmd": "ping"}, tcp_port, timeout=1.0)
        return r.get("status") == "ok"
    except Exception:
        return False


# ── 确保 daemon 在跑 ────────────────────────────────────

def _daemon_args(args) -> list:
    """构造 daemon 子进程命令行。"""

    # monitor/ 目录下的 __main__.py 是 daemon 入口
    monitor_dir = os.path.dirname(os.path.abspath(__file__))
    daemon_script = os.path.join(monitor_dir, "__main__.py")
    script = sys.executable or "python"
    cmd = [
        script, "-u", daemon_script, "--daemon",
        "--transport", str(args.transport),
        "--tcp-port", str(args.tcp_port),
        "--idle", str(args.idle),
        "--exchange-poll", str(args.exchange_poll),
        "--log", str(args.log),
        "--pid-file", str(args.pid_file),
        "--cmd-path", str(args.cmd_path),
        "--out-path", str(args.out_path),
    ]
    if args.transport == "serial":
        cmd += ["--port", str(args.port), "--baud", str(args.baud)]
    elif args.transport == "file":
        cmd += ["--poll-interval", str(args.poll_interval)]
    return cmd


def _ensure_daemon(args, force: bool = False):
    """确保 daemon 在跑。

    1. force=True（open/start）→ 杀死任何已存 daemon，重新启动
    2. force=False → ping 通则复用，否则清僵尸 PID 再启动
    3. 启动失败时提取 inner error（修旧 Bug 5）
    """
    if force:
        try:
            resp = _send_to_daemon({"cmd": "stop"}, args.tcp_port, timeout=1.0)
        except Exception:
            pass
        time.sleep(0.3)

    if not force and port_responds(args.tcp_port):
        return

    pid = read_pid(args.pid_file)
    if pid is not None:
        if is_pid_alive(pid):
            kill_pid(pid)
            time.sleep(0.5)
        clear_pid(args.pid_file)

    daemon_args = _daemon_args(args)
    flags = 0
    for f in ["CREATE_NO_WINDOW", "DETACHED_PROCESS"]:
        if hasattr(subprocess, f):
            flags |= getattr(subprocess, f)
            break
    try:
        proc = subprocess.Popen(
            daemon_args, creationflags=flags, close_fds=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
    except Exception as e:
        print(json.dumps({"status": "error", "error": f"daemon start failed: {e}"}))
        sys.exit(1)

    for _ in range(30):
        time.sleep(0.1)
        if proc.poll() is not None:
            err_raw = proc.stdout.read().decode("utf-8", errors="replace").strip()
            msg = err_raw or f"daemon exited with code {proc.returncode}"
            # 提取 inner error，避免双嵌套 JSON（修旧 Bug 5）
            try:
                inner = json.loads(msg)
                if isinstance(inner, dict) and "error" in inner:
                    msg = inner["error"]
            except (json.JSONDecodeError, ValueError):
                pass
            clear_pid(args.pid_file)
            print(json.dumps({"status": "error", "error": msg}))
            sys.exit(1)
        try:
            if _send_to_daemon({"cmd": "ping"}, args.tcp_port, timeout=0.5).get("status") == "ok":
                return
        except (ConnectionRefusedError, ConnectionResetError,
                ConnectionAbortedError, OSError, socket.timeout):
            continue
    print(json.dumps({"status": "error", "error": "daemon did not start (timeout)"}))
    sys.exit(1)


# ── 客户端调用包装 ───────────────────────────────────────

def _safe_print(call: Callable[[], Any]):
    """调用并 print JSON 响应。任何异常 → JSON 错误。"""
    try:
        print(json.dumps(call()))
    except Exception as e:
        print(json.dumps({"status": "error", "error": str(e)}))


def _client_run(args):
    """dispatch 各子命令。"""
    cmd = args.cmd

    if cmd in ("stop", "close"):
        try:
            resp = _send_to_daemon({"cmd": "stop"}, args.tcp_port)
            print(json.dumps(resp))
            sys.exit(0 if resp.get("status") == "ok" else 1)
        except (ConnectionRefusedError, ConnectionResetError,
                ConnectionAbortedError, OSError,
                socket.timeout, RuntimeError):
            print(json.dumps({"status": "ok", "msg": "daemon not running"}))
            sys.exit(0)

    if cmd in ("start", "open"):
        _ensure_daemon(args, force=True)
        print(json.dumps({"status": "ok", "msg": f"daemon started (idle={args.idle}s)"}))
        sys.exit(0)

    _ensure_daemon(args)

    if cmd == "ping":
        _safe_print(lambda: _send_to_daemon({"cmd": "ping"}, args.tcp_port))
    elif cmd == "write":
        data = " ".join(args.data)
        _safe_print(lambda: _send_to_daemon(
            {"cmd": "write", "data": data, "hex": getattr(args, "hex", False),
             "noeol": getattr(args, "noeol", False)},
            args.tcp_port))
    elif cmd == "exchange":
        data = " ".join(args.data)
        # 修旧 Bug 2：client socket timeout 比 exchange --timeout 多留余量
        sock_timeout = max(args.timeout + 5.0, 8.0)
        _safe_print(lambda: _send_to_daemon(
            {"cmd": "exchange", "data": data, "hex": getattr(args, "hex", False),
             "noeol": getattr(args, "noeol", False),
             "expect": args.expect, "timeout": args.timeout},
            args.tcp_port, timeout=sock_timeout))
    elif cmd == "monitor":
        _run_monitor(args)
    elif cmd == "transfer":
        if args.transfer_cmd == "send":
            from transfer_client import cmd_send
            cmd_send(args)
        else:
            print(json.dumps({"status": "error", "error": f"unknown transfer cmd: {args.transfer_cmd}"}))
            sys.exit(1)
    else:
        print(json.dumps({"status": "error", "error": f"unknown cmd: {cmd}"}))
        sys.exit(1)


def _run_monitor(args):
    """实时流式输出。"""
    hex_mode = getattr(args, "hex", False)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(0.5)
    sock.connect(("127.0.0.1", args.tcp_port))
    deadline = time.time() + args.timeout if args.timeout > 0 else float("inf")
    try:
        sock.sendall(json.dumps({"cmd": "monitor", "hex": hex_mode}).encode("utf-8"))
        while time.time() < deadline:
            try:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                for line in chunk.decode("utf-8").splitlines():
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        obj = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if obj.get("status") == "ok":
                        print(obj.get("data", ""), end="", flush=True)
            except socket.timeout:
                continue
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()