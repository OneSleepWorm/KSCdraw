"""测试 Transport / FileTransport / MockTransport。"""
import os
import threading
import time
import pytest

from transport import Transport, FileTransport, MockTransport, make_transport


# ── 抽象层 / 工厂 ────────────────────────────────────────

def test_transport_abc_cannot_instantiate():
    with pytest.raises(TypeError):
        Transport()


def test_make_transport_unknown():
    with pytest.raises(ValueError):
        make_transport("xxx")


def test_make_transport_mock():
    t = make_transport("mock")
    assert isinstance(t, MockTransport)
    t.close()


# ── MockTransport ───────────────────────────────────────

def test_mock_transport_write_and_outbox():
    t = MockTransport()
    n = t.write(b"hello")
    assert n == 5
    assert t.outbox == b"hello"
    t.close()


def test_mock_transport_read_empty():
    t = MockTransport()
    assert t.read_available() == b""
    t.close()


def test_mock_transport_inject_and_read():
    t = MockTransport()
    t.inject(b"world\r\n")
    assert t.read_available() == b"world\r\n"
    # 二次读应清空
    assert t.read_available() == b""
    t.close()


def test_mock_transport_thread_safety():
    """多线程并发 write 不应丢字节。长度应严格等于所有写入字节之和。"""
    t = MockTransport()

    def writer(n):
        for i in range(100):
            t.write(f"{n:02d}:{i:03d};".encode())  # 固定 7 字节

    threads = [threading.Thread(target=writer, args=(i,)) for i in range(4)]
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    # 4 * 100 * 7 字节 = 2800 字节，固定长度避免数字位数差异
    assert len(t.outbox) == 4 * 100 * 7
    t.close()


def test_mock_transport_reset_outbox():
    t = MockTransport()
    t.write(b"hello")
    t.reset_outbox()
    assert t.outbox == b""
    t.close()


# ── FileTransport ────────────────────────────────────────

def test_file_transport_writes_to_cmd_path(tmp_data_dir):
    cmd = os.path.join(tmp_data_dir, "stdin.txt")
    out = os.path.join(tmp_data_dir, "stdout.txt")
    t = FileTransport(cmd, out)
    n = t.write(b"hello\r\n")
    assert n == 7
    with open(cmd, "rb") as f:
        assert f.read() == b"hello\r\n"
    t.close()


def test_file_transport_clears_cmd_path_on_init(tmp_data_dir):
    """cmd_path 在启动时清空（避免吃上次会话的脏命令）。"""
    cmd = os.path.join(tmp_data_dir, "stdin.txt")
    out = os.path.join(tmp_data_dir, "stdout.txt")
    with open(cmd, "wb") as f:
        f.write(b"stale commands")
    t = FileTransport(cmd, out)
    with open(cmd, "rb") as f:
        assert f.read() == b""
    t.close()


def test_file_transport_preserves_stdout_on_init(tmp_data_dir):
    """Bug 4 修复：out_path 启动时**不**清空，保留 boot 输出。"""
    cmd = os.path.join(tmp_data_dir, "stdin.txt")
    out = os.path.join(tmp_data_dir, "stdout.txt")
    with open(out, "wb") as f:
        f.write(b"term start\r\nterm end\r\n")
    t = FileTransport(cmd, out)
    # 内容仍在
    with open(out, "rb") as f:
        assert f.read() == b"term start\r\nterm end\r\n"
    # 但 read_available 返回空（offset 对到文件尾）
    assert t.read_available() == b""
    t.close()


def test_file_transport_reads_after_offset(tmp_data_dir):
    """读偏移对到 init 时的文件尾，新写入后才能读到。"""
    cmd = os.path.join(tmp_data_dir, "stdin.txt")
    out = os.path.join(tmp_data_dir, "stdout.txt")
    # 初始内容
    with open(out, "wb") as f:
        f.write(b"boot\r\n")
    t = FileTransport(cmd, out)
    # 此时不应读到旧内容
    assert t.read_available() == b""
    # 模拟 KSCOS 写新数据
    with open(out, "ab") as f:
        f.write(b"new output\r\n")
    # 现在应能读到新内容
    assert t.read_available() == b"new output\r\n"
    # offset 推进，再读为空
    assert t.read_available() == b""
    t.close()


def test_file_transport_clear_data(tmp_data_dir):
    """clear_data 清空 cmd + out + 重置 offset。"""
    cmd = os.path.join(tmp_data_dir, "stdin.txt")
    out = os.path.join(tmp_data_dir, "stdout.txt")
    with open(out, "wb") as f:
        f.write(b"old boot\r\n")
    t = FileTransport(cmd, out)
    t.write(b"some cmd\r\n")
    with open(out, "ab") as f:
        f.write(b"new output\r\n")
    # 读完把 offset 推到 16
    t.read_available()
    t.clear_data()
    # 文件被清空
    assert os.path.getsize(cmd) == 0
    assert os.path.getsize(out) == 0
    # offset 复位
    with open(out, "ab") as f:
        f.write(b"X")
    assert t.read_available() == b"X"