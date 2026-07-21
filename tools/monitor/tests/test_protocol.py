"""测试 Daemon 类：协议处理 / lifecycle / 日志。

用 MockTransport，不开 socket 也不开文件，daemon 内部跑 TCP server 部分
单独验证。"""
import json
import os
import socket
import threading
import time

import pytest

from transport import MockTransport
from daemon import Daemon


@pytest.fixture
def mock_daemon(tmp_log_dir, tmp_pid_file):
    """起一个真 daemon（绑定一个空闲 TCP 端口 + MockTransport）。"""
    log_path = os.path.join(tmp_log_dir, "serial.log")
    user_log_path = os.path.join(tmp_log_dir, "serial_user.log")

    # 找一个空闲端口
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    tcp_port = s.getsockname()[1]
    s.close()

    transport = MockTransport()
    d = Daemon(
        transport=transport,
        tcp_port=tcp_port,
        log_path=log_path,
        user_log_path=user_log_path,
        pid_file=tmp_pid_file,
        idle_timeout=60,
        transport_label="mock",
    )
    # 后台线程跑 daemon
    th = threading.Thread(target=d.start, daemon=True)
    th.start()
    # 等 daemon 起来
    for _ in range(30):
        if d.stop.is_set():
            break
        try:
            with socket.create_connection(("127.0.0.1", tcp_port), timeout=0.3):
                break
        except OSError:
            time.sleep(0.05)
    yield d, tcp_port
    # teardown
    d.stop.set()
    # 新建一个连接让 _tcp_server accept 循环退出（accept blocking）
    try:
        with socket.create_connection(("127.0.0.1", tcp_port), timeout=0.3):
            pass
    except OSError:
        pass
    th.join(timeout=2)


# ── 协议处理 ────────────────────────────────────────────

def _send(tcp_port, obj, timeout=3.0):
    sock = socket.socket()
    sock.settimeout(timeout)
    sock.connect(("127.0.0.1", tcp_port))
    sock.sendall((json.dumps(obj) + "\n").encode("utf-8"))
    resp = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        resp += chunk
        if b"\n" in resp:
            break
    sock.close()
    return json.loads(resp.split(b"\n", 1)[0].decode("utf-8"))


def test_daemon_ping(mock_daemon):
    d, port = mock_daemon
    r = _send(port, {"cmd": "ping"})
    assert r == {"status": "ok", "msg": "transport=MockTransport"}


def test_daemon_write_puts_data_to_transport(mock_daemon):
    d, port = mock_daemon
    r = _send(port, {"cmd": "write", "data": "hello"})
    assert r["status"] == "ok"
    # write 自动追 \r\n
    assert d.transport.outbox == b"hello\r\n"


def test_daemon_write_noeol(mock_daemon):
    d, port = mock_daemon
    r = _send(port, {"cmd": "write", "data": "hi", "noeol": True})
    assert r["status"] == "ok"
    assert d.transport.outbox == b"hi"


def test_daemon_write_hex(mock_daemon):
    d, port = mock_daemon
    # 53 54 4f 50 = "STOP"
    r = _send(port, {"cmd": "write", "data": "53544f50", "hex": True})
    assert r["status"] == "ok"
    assert d.transport.outbox == b"STOP\r\n"


def test_daemon_write_unknown_keys(mock_daemon):
    """Daemon 不会因为多余 cmd 字段崩溃。"""
    d, port = mock_daemon
    r = _send(port, {"cmd": "write", "data": "x", "extra_field": "ignored"})
    assert r["status"] == "ok"


def test_daemon_exchange_match(mock_daemon):
    d, port = mock_daemon
    # 让 reader 线程注入假数据回去（mock 模式有 reader 线程 implements_async_read=True）
    # 但 exchange 自己循环 read_available()，reader 可能竞争消费 inbox。
    # 直接在 exchange 之前模拟数据：write 之后 inject 进 transport
    def delayed_inject():
        time.sleep(0.2)
        d.transport.inject(b"OK\r\n")
    threading.Thread(target=delayed_inject, daemon=True).start()
    r = _send(port, {"cmd": "exchange", "data": "AT", "expect": "OK", "timeout": 3.0})
    assert r["status"] == "ok"
    assert r["matched"] == "OK"


def test_daemon_exchange_timeout(mock_daemon):
    d, port = mock_daemon
    # 不 inject 任何数据，expect 必然 miss
    r = _send(port, {"cmd": "exchange", "data": "AT", "expect": "ZZZ", "timeout": 0.5})
    assert r["status"] == "timeout"
    assert r["matched"] == ""


def test_daemon_exchange_no_expect_returns_error(mock_daemon):
    d, port = mock_daemon
    r = _send(port, {"cmd": "exchange", "data": "AT"})
    assert r["status"] == "error"
    assert "expect" in r["error"]


def test_daemon_unknown_command(mock_daemon):
    d, port = mock_daemon
    r = _send(port, {"cmd": "foobar"})
    assert r["status"] == "error"
    assert "unknown" in r["error"]


def test_daemon_invalid_json(mock_daemon):
    d, port = mock_daemon
    sock = socket.socket()
    sock.connect(("127.0.0.1", port))
    sock.sendall(b"not json at all")
    sock.shutdown(socket.SHUT_WR)
    # daemon 应回 invalid JSON 错误
    resp = sock.recv(4096)
    sock.close()
    obj = json.loads(resp.split(b"\n", 1)[0])
    assert obj["status"] == "error"
    assert "invalid JSON" in obj["error"]


def test_daemon_stop(mock_daemon):
    d, port = mock_daemon
    r = _send(port, {"cmd": "stop"})
    assert r["status"] == "ok"
    # daemon 应回到 stopped 状态
    for _ in range(40):
        if d.stop.is_set():
            break
        time.sleep(0.05)
    assert d.stop.is_set()


def test_daemon_writes_pid_file(tmp_log_dir, tmp_pid_file):
    """daemon.start() 应写 PID 文件，stop 后应清。"""
    tcp_port = _free_port()
    transport = MockTransport()
    d = Daemon(
        transport=transport, tcp_port=tcp_port,
        log_path=os.path.join(tmp_log_dir, "serial.log"),
        user_log_path=os.path.join(tmp_log_dir, "serial_user.log"),
        pid_file=tmp_pid_file, idle_timeout=60,
    )
    t = threading.Thread(target=d.start, daemon=True)
    t.start()
    time.sleep(0.3)  # 等 写 PID
    assert os.path.exists(tmp_pid_file)
    pid_in_file = int(open(tmp_pid_file).read())
    assert pid_in_file == os.getpid()  # 子线程也是当前进程

    d.stop.set()
    # 给 _tcp_server accept 循环一个机会退出
    try:
        with socket.create_connection(("127.0.0.1", tcp_port), timeout=0.3):
            pass
    except OSError:
        pass
    t.join(timeout=2)
    assert not os.path.exists(tmp_pid_file)


def _free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port