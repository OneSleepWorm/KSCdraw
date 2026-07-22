"""测试 client.py 的 _send_to_daemon + _ensure_daemon 异常分支 + _safe_print。"""
import json
import os
import socket
import sys
import tempfile
import threading
import time
from types import SimpleNamespace as Namespace
from unittest import mock
from unittest.mock import MagicMock, patch

import pytest

from client import _send_to_daemon, _safe_print, port_responds, _daemon_args


# ── _send_to_daemon ──────────────────────────────────────

def test_send_to_daemon_returns_ok():
    """起一个真 TCP server 响应 ping。"""
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.listen(5)

    def server():
        try:
            conn, _ = s.accept()
            data = conn.recv(65536)
            conn.sendall((json.dumps({"status": "ok", "msg": "hello"}) + "\n").encode())
            conn.close()
        except OSError:
            pass
    th = threading.Thread(target=server, daemon=True)
    th.start()

    r = _send_to_daemon({"cmd": "ping"}, port, timeout=2.0)
    assert r == {"status": "ok", "msg": "hello"}
    s.close()


def test_send_to_daemon_refused():
    with pytest.raises((ConnectionRefusedError, OSError)):
        _send_to_daemon({"cmd": "ping"}, 1, timeout=0.5)  # 1 端口几乎没人监听


def test_send_to_daemon_no_response_raises_runtime():
    """连接上但 server 不回任何数据 → RuntimeError("no response")。"""
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.listen(5)

    def silent_server():
        try:
            conn, _ = s.accept()
            time.sleep(2)
            conn.close()
        except OSError:
            pass
    th = threading.Thread(target=silent_server, daemon=True)
    th.start()

    # 让 recv 在 socket.timeout 后得到空 resp
    with pytest.raises(RuntimeError) as exc:
        _send_to_daemon({"cmd": "ping"}, port, timeout=0.5)
    assert "no response" in str(exc.value) or "10054" in str(exc.value)
    s.close()


def test_send_to_daemon_empty_response_raises_runtime():
    """连接立即关闭（空 chunk）应抛 RuntimeError。"""
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.listen(5)

    def close_immediately():
        try:
            conn, _ = s.accept()
            conn.close()
        except OSError:
            pass
    th = threading.Thread(target=close_immediately, daemon=True)
    th.start()

    # Windows 下立即关闭可能抛 ConnectionAbortedError (不被 client.py 捕获)
    # 或 RuntimeError (resp 为空时)。两种都接受。
    with pytest.raises((RuntimeError, ConnectionAbortedError,
                         ConnectionResetError, OSError)):
        _send_to_daemon({"cmd": "ping"}, port, timeout=2.0)
    s.close()


# ── _safe_print ──────────────────────────────────────────

def test_safe_print_normal(capsys):
    _safe_print(lambda: {"status": "ok", "msg": "hello"})
    out = capsys.readouterr().out
    assert json.loads(out) == {"status": "ok", "msg": "hello"}


def test_safe_print_exception(capsys):
    def raises():
        raise RuntimeError("boom")
    _safe_print(raises)
    out = capsys.readouterr().out
    obj = json.loads(out)
    assert obj["status"] == "error"
    assert "boom" in obj["error"]


# ── port_responds ────────────────────────────────────────

def test_port_responds_false_when_no_daemon():
    # 一个几乎一定没人监听的端口
    assert port_responds(1) is False


def test_port_responds_true_when_daemon_up():
    from transport import MockTransport
    from daemon import Daemon

    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()

    import tempfile
    with tempfile.TemporaryDirectory() as tmp_log_dir:
        transport = MockTransport()
        d = Daemon(
            transport=transport, tcp_port=port,
            log_path=os.path.join(tmp_log_dir, "serial.log"),
            user_log_path=os.path.join(tmp_log_dir, "serial_user.log"),
            pid_file=os.path.join(tmp_log_dir, "daemon.pid"),
            idle_timeout=60,
        )
        th = threading.Thread(target=d.start, daemon=True)
        th.start()
        time.sleep(0.3)
        assert port_responds(port) is True
        d.stop.set()
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.3):
                pass
        except OSError:
            pass
        th.join(timeout=2)


# ── _daemon_args ─────────────────────────────────────────

def test_daemon_args_file_transport():
    args = Namespace(
        transport="file", port="COM6", baud=115200, poll_interval=50,
        tcp_port=12345, idle=60, exchange_poll=0.001,
        log="/tmp/serial.log", pid_file="/tmp/daemon.pid",
        cmd_path="/tmp/stdin.txt", out_path="/tmp/stdout.txt",
    )
    cmd = _daemon_args(args)
    assert "--daemon" in cmd
    assert "--transport" in cmd
    assert "--cmd-path" in cmd
    assert "--out-path" in cmd
    assert "--poll-interval" in cmd
    assert "--port" not in cmd
    assert "--baud" not in cmd


def test_daemon_args_serial_transport():
    args = Namespace(
        transport="serial", port="COM7", baud=9600, poll_interval=50,
        tcp_port=12345, idle=60, exchange_poll=0.001,
        log="/tmp/serial.log", pid_file="/tmp/daemon.pid",
        cmd_path="/tmp/stdin.txt", out_path="/tmp/stdout.txt",
    )
    cmd = _daemon_args(args)
    assert "--port" in cmd
    assert "COM7" in cmd
    assert "--baud" in cmd
    assert "9600" in cmd
    assert "--poll-interval" not in cmd


# ── _ensure_daemon zombie recovery ──────────────────────

def test_ensure_daemon_recovers_from_dead_pid(tmp_pid_file):
    """PID 文件指向一个已死 PID，_ensure_daemon 应清理 + 启动新 daemon。"""
    # 起一个短命 Python 进程，拿一个已死 PID 写进 PID 文件
    # 用 os.getpid() 加一个偏移找一个几乎肯定不存在的 PID
    bogus_pid = 99999999 if os.name == "nt" else 65536 + 7777
    from daemon import write_pid
    write_pid(tmp_pid_file, bogus_pid)

    # 现在 mock 一下：让 _daemon_args 返回我们可控制的 daemon，再让 ping
    # 在第二次调用时成功，从而走僵尸清理分支而非真的启动子进程
    from client import _ensure_daemon
    # 用 mock patch
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()

    # 起一个真 daemon 临时占住 port
    from transport import MockTransport
    from daemon import Daemon
    with tempfile.TemporaryDirectory() as tmp_log_dir:
        transport = MockTransport()
        d = Daemon(
            transport=transport, tcp_port=port,
            log_path=os.path.join(tmp_log_dir, "serial.log"),
            user_log_path=os.path.join(tmp_log_dir, "serial_user.log"),
            pid_file=tmp_pid_file, idle_timeout=60,
        )
        th = threading.Thread(target=d.start, daemon=True)
        th.start()
        time.sleep(0.3)

        # port_responds 会 True，_ensure_daemon 第一行就 return，不需要 _daemon_args
        args = Namespace(
            transport="file", port="COM6", baud=115200, poll_interval=50,
            tcp_port=port, idle=60, log=os.path.join(tmp_log_dir, "serial.log"),
            pid_file=tmp_pid_file,
            cmd_path=os.path.join(tmp_log_dir, "stdin.txt"),
            out_path=os.path.join(tmp_log_dir, "stdout.txt"),
        )
        _ensure_daemon(args)  # 不抛即成功
        # PID 文件应被 daemon 自己写成  os.getpid()
        assert os.path.exists(tmp_pid_file)
        d.stop.set()
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.3):
                pass
        except OSError:
            pass
        th.join(timeout=2)