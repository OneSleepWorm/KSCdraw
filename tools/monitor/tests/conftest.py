"""monitor 包单元测试 fixture。"""
import os
import sys
import tempfile
import pytest

# 把 monitor/ 目录加到 sys.path，让 `from transport import X` 这种 sibling import 在
# 测试中可见。__main__.py 在生产运行时已做这件事，但要 -m pytest 时本目录不一定在。
MONITOR_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if MONITOR_DIR not in sys.path:
    sys.path.insert(0, MONITOR_DIR)


@pytest.fixture
def tmp_log_dir(tmp_path):
    """临时 logs 目录。"""
    d = tmp_path / "logs"
    d.mkdir()
    return str(d)


@pytest.fixture
def tmp_data_dir(tmp_path):
    """临时 .data 目录。"""
    d = tmp_path / ".data"
    d.mkdir()
    return str(d)


@pytest.fixture
def tmp_pid_file(tmp_log_dir):
    return os.path.join(tmp_log_dir, "daemon.pid")