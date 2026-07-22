#!/usr/bin/env python3
"""daemon.py — daemon 子进程逻辑 + PID 文件 + 日志助手。

职责:
- 日志格式化函数 log_entry
- PID 文件 write/read/clear/is_pid_alive（跨平台）
- Daemon 类：监听 127.0.0.1 端口，dispatch JSON 命令，read 后台线程
- run_daemon(args)：daemon 子进程入口
"""
from __future__ import annotations

import atexit
import json
import os
import socket
import subprocess
import sys
import threading
import time
from datetime import datetime
from typing import Optional

from transport import make_transport, Transport


# ── 日志 ────────────────────────────────────────────────

def log_entry(dir_: str, data: bytes, tag: str = "") -> str:
    """格式化单行日志。"""
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
    label = f"{dir_}({tag})" if tag else dir_
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        text = data.hex()
    escaped = repr(text)[1:-1]
    return f"[{ts}][{label}] {escaped}\n"


# ── PID 文件管理 ─────────────────────────────────────────

def write_pid(pid_file: str, pid: int):
    try:
        os.makedirs(os.path.dirname(pid_file) or ".", exist_ok=True)
        with open(pid_file, "w") as f:
            f.write(str(pid))
    except OSError:
        pass


def clear_pid(pid_file: str):
    try:
        if os.path.exists(pid_file):
            os.remove(pid_file)
    except OSError:
        pass


def read_pid(pid_file: str) -> Optional[int]:
    try:
        with open(pid_file, "r") as f:
            return int(f.read().strip())
    except (OSError, ValueError):
        return None


def is_pid_alive(pid: int) -> bool:
    """跨平台 PID 存活检测，不依赖 psutil。"""
    if pid <= 0:
        return False
    try:
        if os.name == "nt":
            r = subprocess.run(
                ["tasklist", "/FI", f"PID eq {pid}", "/NH", "/FO", "CSV"],
                capture_output=True, text=True, timeout=2,
            )
            return str(pid) in r.stdout
        else:
            os.kill(pid, 0)  # signal 0 = 探测
            return True
    except (OSError, subprocess.SubprocessError):
        return False


def kill_pid(pid: int) -> bool:
    """跨平台强杀进程。返回是否尝试成功。"""
    try:
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(pid), "/F"],
                           capture_output=True, timeout=3)
        else:
            import signal as _sig
            os.kill(pid, _sig.SIGTERM)
        return True
    except (OSError, subprocess.SubprocessError):
        return False


# ── Daemon ──────────────────────────────────────────────

class Daemon:
    """daemon 实体。监听 127.0.0.1:tcp_port，dispatch JSON 命令到 transport。"""

    def __init__(self, transport: Transport, tcp_port: int,
                 log_path: str, user_log_path: str, pid_file: str,
                 idle_timeout: int, transport_label: str = "",
                 exchange_poll_interval: float = 0.001):
        self.transport = transport
        self.tcp_port = tcp_port
        self.log_path = log_path
        self.user_log_path = user_log_path
        self.pid_file = pid_file
        self.idle_timeout = idle_timeout
        self.transport_label = transport_label
        self.exchange_poll_interval = exchange_poll_interval
        self.stop = threading.Event()

    def start(self):
        """daemon 子进程主入口。"""
        # 日志文件初始清空
        os.makedirs(os.path.dirname(self.log_path) or ".", exist_ok=True)
        with open(self.log_path, "w", encoding="utf-8") as f:
            f.write("")
        with open(self.user_log_path, "w", encoding="utf-8") as f:
            f.write("")
        label = self.transport_label or type(self.transport).__name__
        self._log("系统", f"Daemon started ({label})".encode())
        # 写 PID + atexit 兜底清理
        write_pid(self.pid_file, os.getpid())
        atexit.register(clear_pid, self.pid_file)
        # reader 线程：仅对 implements_async_read=True 的 transport
        if self.transport.implements_async_read:
            rd = threading.Thread(target=self._reader, daemon=True)
            rd.start()
        try:
            self._tcp_server()
        finally:
            # stop 路径：用户 close、idle 超时或异常
            if self.transport.supports_clear():
                self.transport.clear_data()  # 清 stdin.txt + stdout.txt
            self.transport.close()
            clear_pid(self.pid_file)
            self._log("系统", b"Daemon stopped")

    def _reader(self):
        while not self.stop.is_set():
            try:
                data = self.transport.read_available()
                if data:
                    self._log("收", data)
                else:
                    time.sleep(self.transport.poll_interval)
            except Exception:
                break

    def _tcp_server(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("127.0.0.1", self.tcp_port))
        sock.listen(5)
        sock.settimeout(0.5)
        last_active = time.time()

        while not self.stop.is_set():
            try:
                conn, addr = sock.accept()
            except socket.timeout:
                if time.time() - last_active > self.idle_timeout:
                    self._log("系统", b"Idle timeout, shutting down")
                    break
                continue
            last_active = time.time()
            t = threading.Thread(target=self._handle_conn, args=(conn,))
            t.daemon = True
            t.start()

        self.stop.set()
        sock.close()

    def _handle_conn(self, conn):
        try:
            raw = conn.recv(65536)
            if not raw:
                return
            cmd = json.loads(raw.decode("utf-8"))
            self._dispatch(conn, cmd)
        except (json.JSONDecodeError, UnicodeDecodeError):
            self._send_json(conn, {"status": "error", "error": "invalid JSON"})
        except Exception:
            pass
        finally:
            try:
                conn.close()
            except Exception:
                pass

    def _dispatch(self, conn, cmd):
        handlers = {
            "ping":     self._h_ping,
            "write":    self._h_write,
            "exchange": self._h_exchange,
            "monitor":  self._h_monitor,
            "stop":     self._h_stop,
        }
        h = handlers.get(cmd.get("cmd", ""))
        if h:
            h(conn, cmd)
        else:
            self._send_json(conn, {"status": "error",
                                    "error": f"unknown cmd: {cmd.get('cmd')}"})

    def _send_json(self, conn, obj):
        try:
            conn.sendall((json.dumps(obj, ensure_ascii=False) + "\n").encode("utf-8"))
        except Exception:
            pass

    def _log(self, dir_: str, data: bytes, tag: str = ""):
        if not data:
            return
        try:
            with open(self.log_path, "a", encoding="utf-8") as f:
                f.write(log_entry(dir_, data, tag))
        except OSError:
            pass
        if dir_ == "收":
            try:
                text = data.decode("utf-8", errors="replace")
                text = text.replace("\r\n", "\n").replace("\r", "").lstrip("\n")
                with open(self.user_log_path, "a", encoding="utf-8") as f:
                    f.write(text)
            except OSError:
                pass

    def _prepare(self, data_str, hex_mode):
        if hex_mode:
            return bytes.fromhex(data_str.replace(" ", "")), "HEX"
        return data_str.encode("utf-8"), "TXT"

    # ── 协议处理器 ──────────────────────────────────────

    def _h_ping(self, conn, cmd):
        self._send_json(conn, {
            "status": "ok",
            "msg": f"transport={type(self.transport).__name__}",
        })

    def _h_write(self, conn, cmd):
        try:
            raw, tag = self._prepare(cmd["data"], cmd.get("hex", False))
            if not cmd.get("noeol"):
                raw += b"\r\n"
            n = self.transport.write(raw)
            self._log("发", raw, tag)
            self._send_json(conn, {"status": "ok", "sent": n})
        except Exception as e:
            self._send_json(conn, {"status": "error", "error": str(e)})

    def _h_exchange(self, conn, cmd):
        try:
            hex_mode = cmd.get("hex", False)
            raw, tag = self._prepare(cmd["data"], hex_mode)
            if not cmd.get("noeol"):
                raw += b"\r\n"
            expect_str = cmd.get("expect")   # None = 无 expect 模式
            timeout = cmd.get("timeout", 3.0)
            QUIET = 0.2                      # 静默期 200ms：收到首帧后无新数据即返回

            self.transport.read_available()  # 丢旧数据 / 对齐 offset
            self.transport.write(raw)
            self._log("发", raw, tag)

            collected = ""
            matched = ""
            end = time.time() + timeout
            last_data_t = None               # 首帧时间戳
            while time.time() < end:
                chunk = self.transport.read_available()
                if chunk:
                    self._log("收", chunk)
                    s = chunk.hex() if hex_mode else chunk.decode("utf-8", errors="replace")
                    collected += s
                    last_data_t = time.time()
                    if expect_str and expect_str in collected:
                        matched = expect_str
                        break
                else:
                    time.sleep(self.exchange_poll_interval)
                    # 无 --expect 模式：收到过数据 + 静默期满 → 返回
                    if not expect_str and last_data_t is not None:
                        if time.time() - last_data_t >= QUIET:
                            break

            if expect_str:
                status = "ok" if matched else "timeout"
            else:
                status = "ok" if collected else "timeout"
            self._send_json(conn, {
                "status": status, "sent": len(raw),
                "received": collected, "matched": matched,
                "length": len(collected),
            })
        except Exception as e:
            self._send_json(conn, {"status": "error", "error": str(e)})

    def _h_monitor(self, conn, cmd):
        hex_mode = cmd.get("hex", False)
        conn.settimeout(1.0)
        try:
            while not self.stop.is_set():
                try:
                    chunk = self.transport.read_available()
                    if not chunk:
                        time.sleep(self.transport.poll_interval)
                        continue
                except Exception:
                    break
                s = chunk.hex() if hex_mode else chunk.decode("utf-8", errors="replace")
                self._log("收", chunk)
                self._send_json(conn, {"status": "ok", "data": s, "length": len(chunk)})
        except Exception:
            pass

    def _h_stop(self, conn, cmd):
        self._send_json(conn, {"status": "ok"})
        self.stop.set()


# ── daemon 子进程入口 ────────────────────────────────────

def run_daemon(args):
    """子进程入口（被 cli.py 的 --daemon 分支调用）。

    期望的 args 字段：
      transport, port, baud, poll_interval, tcp_port, idle, log, pid_file,
      cmd_path, out_path
    """
    # 吃掉所有异常：stdout 只输出干净的 JSON 错误，traceback 走 stderr 但子进程
    # 用了 stdout=PIPE stderr=STDOUT，混合后会双嵌套。这里 sys.exit 防止 raise
    # 再喷 traceback。
    try:
        transport = make_transport(
            args.transport,
            port=args.port, baud=args.baud,
            poll_interval_ms=args.poll_interval,
            cmd_path=args.cmd_path, out_path=args.out_path,
        )
        user_log_path = os.path.join(
            os.path.dirname(args.log), "serial_user.log")
        Daemon(
            transport=transport,
            tcp_port=args.tcp_port,
            log_path=args.log,
            user_log_path=user_log_path,
            pid_file=args.pid_file,
            idle_timeout=args.idle,
            exchange_poll_interval=args.exchange_poll,
            transport_label=args.transport,
        ).start()
    except Exception as e:
        # 子进程 stdout 被 _ensure_daemon 捕获 → 提取 inner error 用
        print(json.dumps({"status": "error", "error": str(e)}), flush=True)
        sys.exit(1)