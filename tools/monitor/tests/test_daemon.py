"""测试日志格式化 + PID 文件管理。"""
import os
import sys
import pytest

from daemon import log_entry, write_pid, read_pid, clear_pid, is_pid_alive, kill_pid


# ── log_entry ───────────────────────────────────────────

def test_log_entry_basic():
    line = log_entry("发", b"hello", "TXT")
    assert line.startswith("[")
    assert "][发(TXT)] hello" in line
    assert line.endswith("\n")


def test_log_entry_no_tag():
    line = log_entry("系统", b"started")
    assert "][系统] started" in line


def test_log_entry_non_ascii_falls_back_to_hex():
    """非 UTF-8 数据回退到 hex 表示。"""
    bad = b"\xff\xfe"
    line = log_entry("收", bad)
    assert "fffe" in line  # hex


def test_log_entry_escapes_newlines():
    line = log_entry("收", b"a\r\nb")
    assert "\r" not in line.rstrip("\n")
    assert "\\r\\n" in line  # repr 转义后


# ── PID 文件 ─────────────────────────────────────────────

def test_pid_write_read_cycle(tmp_pid_file):
    write_pid(tmp_pid_file, 12345)
    assert read_pid(tmp_pid_file) == 12345


def test_pid_read_missing(tmp_pid_file):
    assert read_pid(tmp_pid_file) is None


def test_pid_read_corrupted(tmp_pid_file):
    with open(tmp_pid_file, "w") as f:
        f.write("not a number")
    assert read_pid(tmp_pid_file) is None


def test_pid_clear_existing(tmp_pid_file):
    write_pid(tmp_pid_file, 12345)
    clear_pid(tmp_pid_file)
    assert not os.path.exists(tmp_pid_file)


def test_pid_clear_missing_silent(tmp_pid_file):
    # 不应抛异常
    clear_pid(tmp_pid_file)


def test_pid_write_creates_parent_dir(tmp_path):
    pid_file = os.path.join(str(tmp_path), "subdir", "another", "daemon.pid")
    write_pid(pid_file, 99)
    assert os.path.exists(pid_file)
    assert read_pid(pid_file) == 99


# ── is_pid_alive / kill_pid ───────────────────────────────

def test_is_pid_alive_self():
    assert is_pid_alive(os.getpid()) is True


def test_is_pid_alive_invalid():
    assert is_pid_alive(0) is False
    assert is_pid_alive(-1) is False


def test_is_pid_alive_nonexistent():
    # 在 Windows PID 0xFFFFFFFF 几乎一定不存在；Unix 找一个超出 PID_MAX 的
    if os.name == "nt":
        bogus = 99999999
    else:
        bogus = 2 ** 22  # 远超 /proc/sys/kernel/pid_max
    assert is_pid_alive(bogus) is False


def test_kill_pid_nonexistent_silent():
    """杀一个不存在的 PID 不应抛异常。"""
    bogus = 99999999 if os.name == "nt" else 2 ** 22
    # 不抛即可
    result = kill_pid(bogus)
    # 返回值意义：是否尝试成功；不存在的 PID 可能 False 也接受
    assert result in (True, False)