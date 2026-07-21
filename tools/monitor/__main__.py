#!/usr/bin/env python3
"""monitor 包入口。`python KSCOS/tools/monitor <cmd> ...` 直接跑这里。

为了让 `from cli import main` / `from transport import X` 这种 sibling
import 在「作为脚本运行」和「作为一个包跑 -m」两种模式下都能工作，先把
本目录塞进 sys.path。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from cli import main

if __name__ == "__main__":
    main()