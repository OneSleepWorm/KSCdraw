#!/usr/bin/env python3
"""api_smoke.py — 核心模块 (littlefs FS + KSCGUI) 跨平台冒烟验证。

对每条用例: 通过 monitor daemon 发送命令 → 等输出静默 → 本地断言期望子串。
一次脚本 = 开 daemon + 跑完全部用例 + 汇总 PASS/FAIL + 关 daemon。

只验证两个核心模块 (FS / GUI) 的 appcmd 接口，PC 与 STM32 行为应一致。

用法:
    # STM32 (串口) — 板子需已烧录 KSCOS 固件
    python tools/api_smoke.py --transport serial --port COM6 --baud 115200

    # PC (file transport, 默认) — 需先构建并运行 build_debug/KSCOS.exe
    python tools/api_smoke.py

    # 只跑名字包含某关键字的用例
    python tools/api_smoke.py --filter littlefs

退出码: 0=全部通过, 1=有用例失败
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))          # KSCOS/tools/
_MONITOR_DIR = os.path.join(_SCRIPT_DIR, "monitor")

sys.path.insert(0, _MONITOR_DIR)
from client import _send_to_daemon  # noqa: E402

DEFAULT_PORT = "COM6"
DEFAULT_BAUD = 115200
DEFAULT_TCP_PORT = 12345


# ── 用例表 ──────────────────────────────────────────────────
# (名字, 命令, [可接受子串...] 任一命中即 PASS)
# 注意 appcmd 输出类命令的优先级: user_data → output_fn → -1，
# terminal 走 output_fn, 板级响应会带 "ok:N" 或 "error:N" 尾缀。
#
# 仅覆盖核心模块 littlefs FS + KSCGUI，PC 与 STM32 appcmd 接口应一致。
CASES = [
    # ── littlefs FS ──
    ("littlefs mount(幂等)",    "littlefs mount",            ["ok:0", "error:-1"]),
    ("littlefs ls /",           "littlefs ls -p /",          ["home", "bin", "sys"]),
    ("littlefs mkdir",          "littlefs mkdir -p /smoke",  ["ok:0", "error:-1"]),
    ("littlefs writenew",       "littlefs writenew -p /smoke/t.txt -d smoke", ["ok:5"]),
    ("littlefs cat 回读",       "littlefs cat -p /smoke/t.txt", ["smoke"]),
    ("littlefs stat",           "littlefs stat -p /smoke/t.txt", ["size=5", "t.txt"]),
    ("littlefs mv 改名",        "littlefs mv -s /smoke/t.txt -d /smoke/t2.txt", ["ok:0"]),
    ("littlefs rm 清理",        "littlefs rm -p /smoke/t2.txt", ["ok:0"]),
    ("littlefs rmdir 清理",     "littlefs rm -p /smoke",     ["ok:0"]),

    # ── KSCGUI ──
    # STM32 上必须先 `init` 跑 ST7789 初始化时序才能出屏 (PC 幂等空操作)。
    # 绘制元素在红底上叠加，跑完屏幕应为: 红底 + 彩色几何元素 + 左上白块。
    ("help 注册表含 KSCGUI",     "help",                      ["KSCGUI", "littlefs"]),
    ("KSCGUI init 初始化屏",     "KSCGUI init",               ["ok:"]),
    ("KSCGUI fill 全屏红色",     "KSCGUI fill -x 0 -y 0 -w 240 -h 320 -c F800", ["ok:"]),
    ("KSCGUI pixel 单像素",      "KSCGUI pixel -x 10 -y 10 -c FFFF", ["ok:"]),
    ("KSCGUI line 直线",        "KSCGUI line -x 0 -y 0 -w 240 -z 320 -c FFFF", ["ok:"]),
    ("KSCGUI rect 矩形",        "KSCGUI rect -x 30 -y 30 -w 60 -h 40 -c 07E0", ["ok:"]),
    ("KSCGUI fillbox 填充矩形",  "KSCGUI fill -x 100 -y 30 -w 50 -h 40 -c 001F", ["ok:"]),
    ("KSCGUI circle 空心圆",    "KSCGUI circle -x 60 -y 130 -r 40 -c FFFF", ["ok:"]),
    ("KSCGUI fcircle 实心圆",   "KSCGUI fcircle -x 140 -y 130 -r 35 -c FFE0", ["ok:"]),
    ("KSCGUI arc 圆弧",         "KSCGUI arc -x 200 -y 130 -r 40 -d 1 -c FFFF", ["ok:"]),
    ("KSCGUI rrect 圆角矩形",   "KSCGUI rrect -x 30 -y 200 -w 60 -h 40 -r 10 -c 07E0", ["ok:"]),
    ("KSCGUI frrect 填充圆角",  "KSCGUI frrect -x 100 -y 200 -w 60 -h 40 -r 10 -c F81F", ["ok:"]),
    ("KSCGUI char 字符",        "KSCGUI char -x 30 -y 270 -v 65 -c FFFF -b 0000", ["ok:"]),
    ("KSCGUI string 字符串",    "KSCGUI string -x 50 -y 270 -s KSC -c FFFF -b 0000", ["ok:"]),
    ("KSCGUI wcreate 建窗",     "KSCGUI wcreate -x 0 -y 0 -w 100 -h 100 -c FFFF", ["ok:"]),
    ("KSCGUI wselect 选中",     "KSCGUI wselect -h 0",       ["ok:", "error:"]),
]


def open_daemon(args) -> None:
    cmd = [sys.executable, "-u", os.path.join(_MONITOR_DIR, "__main__.py"),
           "open", "--transport", args.transport,
           "--tcp-port", str(args.tcp_port),
           "--idle", str(args.idle)]
    if args.transport == "serial":
        cmd += ["--port", args.port, "--baud", str(args.baud)]
    subprocess.run(cmd, check=True)


def close_daemon(args) -> None:
    cmd = [sys.executable, "-u", os.path.join(_MONITOR_DIR, "__main__.py"),
           "close", "--tcp-port", str(args.tcp_port)]
    subprocess.run(cmd)


def exchange(args, cmd: str, timeout: float) -> str:
    resp = _send_to_daemon(
        {"cmd": "exchange", "data": cmd, "noeol": False,
         "expect": None, "timeout": timeout},
        args.tcp_port, timeout=timeout + 5.0)
    if resp.get("status") != "ok":
        return resp.get("received", "") + " [status=" + str(resp.get("status")) + "]"
    return resp.get("received", "")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--transport", choices=["serial", "file"], default="file")
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument("--tcp-port", type=int, default=DEFAULT_TCP_PORT)
    ap.add_argument("--idle", type=int, default=120)
    ap.add_argument("--filter", default="", help="只跑名字含该子串的用例")
    ap.add_argument("--timeout", type=float, default=4.0, help="单条命令超时(秒)")
    ap.add_argument("--keep-open", action="store_true", help="跑完不关 daemon")
    args = ap.parse_args()

    cases = [(n, c, e) for (n, c, e) in CASES
             if not args.filter or args.filter.lower() in n.lower()]

    print(f"[api_smoke] transport={args.transport} "
          f"{'port=' + args.port if args.transport == 'serial' else 'file'} "
          f"cases={len(cases)}")
    open_daemon(args)
    time.sleep(0.3)

    passed = failed = 0
    for name, cmd, expects in cases:
        try:
            out = exchange(args, cmd, args.timeout)
        except Exception as e:
            out = "[exception] " + str(e)
        if any(x in out for x in expects):
            passed += 1
            print(f"  PASS  {name:28s} <- {cmd}")
        else:
            failed += 1
            print(f"  FAIL  {name:28s} <- {cmd}")
            print(f"        expected any of {expects}")
            print(f"        got {out!r}")

    if not args.keep_open:
        close_daemon(args)

    print(f"\nresult: {passed} passed, {failed} failed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
