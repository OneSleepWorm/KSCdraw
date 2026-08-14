#!/usr/bin/env python3
"""symaddr.py — 构建期查询 app 系列函数入口地址。

用途:
  从链接产物 (.elf / .exe) 里查询 appget/appopen/appcmd 等 app 系列
  函数的入口地址, 或全量导出为 JSON 供上位机 / 构建脚本使用。

  符号来源:
    - STM32 固件  build/Debug/KSCOS.elf  (链接地址 = 运行时地址)
    - PC 程序    build_debug/KSCOS.exe   (PE 内 RVA, 运行时受 ASLR 影响)

  背景:
    STM32 用 -flto, 未被外部引用的函数会被 LTO 内联/局部化。为稳定导出,
    src/app.c 用 __attribute__((used, noipa)) 保号, 链接脚本用强赋值导出
    app*_entry 别名 (appget_entry = appget), 使符号对任意链接方可见可链接。

用法:
  python tools/symaddr.py appget appcmd          # 查指定函数地址
  python tools/symaddr.py --all                  # 全量导出 8 个入口
  python tools/symaddr.py --json                 # JSON 输出 (供程序消费)
  python tools/symaddr.py --elf build/Debug/KSCOS.elf   # 指定 STM32 elf
  python tools/symaddr.py --exe build_debug/KSCOS.exe   # 指定 PC exe

返回码: 0 = 全部找到; 1 = 至少一个未找到
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys

# app 系列 API 入口 (src/app.c)
APP_API_SYMBOLS = [
    "appget",
    "appopen",
    "appclose",
    "appread",
    "appwrite",
    "appcmd",
    "appcmd_argv",
    "appfree",
]

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ELF = os.path.join(ROOT, "build", "Debug", "KSCOS.elf")
DEFAULT_EXE = os.path.join(ROOT, "build_debug", "KSCOS.exe")

# 常见 arm-none-eabi nm 路径 (可在 PATH 或显式指定)
ARM_NM_CANDIDATES = [
    "arm-none-eabi-nm",
    os.path.expanduser("~/arm-gnu-toolchain*/bin/arm-none-eabi-nm.exe"),
]


def find_nm() -> str:
    """返回 arm-none-eabi nm 可执行路径; PC (MinGW) 返回 'nm'。"""
    exe = shutil.which("arm-none-eabi-nm")
    if exe:
        return exe
    # 常见安装目录兜底
    for cand in ARM_NM_CANDIDATES[1:]:
        import glob
        hits = glob.glob(cand)
        if hits:
            return hits[0]
    # 最后尝试 MinGW nm (PC 无 arm 工具链时)
    return "nm"


def _parse_nm_output(nm: str, path: str) -> dict:
    """调用 nm 解析符号 → {名字: (地址, 类型)}。"""
    try:
        out = subprocess.run([nm, path], capture_output=True, text=True,
                             check=False).stdout
    except FileNotFoundError:
        raise SystemExit(f"nm not found: {nm}. Install arm-none-eabi toolchain "
                         f"or MinGW, or pass a working nm via PATH.")
    result = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        addr, symtype = parts[0], parts[1]
        name = parts[2] if len(parts) > 2 else ""
        # PE 符号 (PC) 可能有 'U' (undefined) / 'v' 等非地址项, 跳过
        try:
            addrval = int(addr, 16)
        except ValueError:
            continue
        result[name] = (addrval, symtype)
    return result


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("symbols", nargs="*", help="要查询的符号名 (默认全部)")
    ap.add_argument("--all", action="store_true",
                    help="导出全部 app API 入口")
    ap.add_argument("--json", action="store_true",
                    help="JSON 输出")
    ap.add_argument("--elf", default=DEFAULT_ELF, help="STM32 固件路径")
    ap.add_argument("--exe", default=DEFAULT_EXE, help="PC 程序路径")
    ap.add_argument("--nm", default=None, help="nm 可执行路径")
    args = ap.parse_args()

    # 平台判定: --elf 指定且存在 → STM32; 否则 --exe 存在 → PC
    if os.path.exists(args.elf):
        path, platform = args.elf, "stm32"
        nm = args.nm or find_nm()
    elif os.path.exists(args.exe):
        path, platform = args.exe, "pc"
        nm = args.nm or shutil.which("nm") or find_nm()
    else:
        raise SystemExit(f"no build artifact found:\n  elf={args.elf}\n  exe={args.exe}\n"
                         f"Build first, then retry.")

    symbols = args.symbols
    if args.all or not symbols:
        symbols = APP_API_SYMBOLS
    # 同时接受 entry 别名
    symbols = [s for s in symbols]

    table = _parse_nm_output(nm, path)

    found = {}
    missing = []
    for s in symbols:
        # 优先精确名, 其次 entry 别名
        if s in table:
            found[s] = table[s]
        elif f"{s}_entry" in table:
            found[s] = table[f"{s}_entry"]
        else:
            missing.append(s)

    if args.json:
        payload = {
            "platform": platform,
            "artifact": path,
            "note": ("RVA, runtime address varies due to ASLR"
                     if platform == "pc"
                     else "absolute, equals runtime address"),
            "entries": {s: f"0x{a:08x}" for s, (a, _t) in found.items()},
            "missing": missing,
        }
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        for s in symbols:
            if s in found:
                addr, symtype = found[s]
                print(f"{s:<14} {symtype} 0x{addr:08x}")
            else:
                print(f"{s:<14} MISSING")
        print(f"[symaddr] platform={platform} artifact={path}")
        if missing:
            print(f"[symaddr] missing: {', '.join(missing)}")

    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
