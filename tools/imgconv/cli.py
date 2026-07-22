from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile

from PIL import Image

_MAX_W = 240
_MAX_H = 240

_TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
_SCRIPT_DIR = os.path.dirname(_TOOL_DIR)
_APPDATA_DIR = os.path.join(_SCRIPT_DIR, "appdata", "imgconv")
_USERDATA_DIR = os.path.join(_SCRIPT_DIR, "userdata")


def _resolve_input(path: str) -> str:
    if os.path.isabs(path):
        if os.path.isfile(path):
            return path
        raise FileNotFoundError(f"not found: {path}")

    if os.path.isfile(path):
        return os.path.abspath(path)

    userpath = os.path.join(_USERDATA_DIR, path)
    if os.path.isfile(userpath):
        return userpath

    raise FileNotFoundError(f"not found: {path} (tried CWD and {_USERDATA_DIR})")


def _default_output(src: str) -> str:
    os.makedirs(_APPDATA_DIR, exist_ok=True)
    name = os.path.splitext(os.path.basename(src))[0] + ".bmp"
    return os.path.join(_APPDATA_DIR, name)


def _fit_size(w: int, h: int, max_w: int, max_h: int) -> tuple:
    if w <= max_w and h <= max_h:
        return w, h
    ratio = min(max_w / w, max_h / h)
    return int(w * ratio), int(h * ratio)


def _convert(src: str, dst: str, size: tuple | None):
    img = Image.open(src)
    if img.mode != "RGB":
        img = img.convert("RGB")

    if size:
        img = img.resize(size, Image.LANCZOS)
    else:
        nw, nh = _fit_size(img.width, img.height, _MAX_W, _MAX_H)
        if (nw, nh) != (img.width, img.height):
            img = img.resize((nw, nh), Image.LANCZOS)

    img.save(dst, "BMP")
    return img.size


def cmd_convert(args):
    src = _resolve_input(args.input)
    dst = args.output or _default_output(src)

    if args.size:
        parts = args.size.lower().split("x")
        if len(parts) != 2:
            print("error: -s format: <width>x<height>", file=sys.stderr)
            sys.exit(1)
        size = (int(parts[0]), int(parts[1]))
    else:
        size = None

    result = _convert(src, dst, size)
    print(f"{src} -> {dst} ({result[0]}x{result[1]})")

    if args.upload:
        _upload(dst, args.upload)


def _upload(local: str, remote: str):
    monitor_pkg = os.path.join(_SCRIPT_DIR, "monitor")
    if not os.path.isdir(monitor_pkg):
        print(f"error: monitor package not found at {monitor_pkg}", file=sys.stderr)
        sys.exit(1)

    r = subprocess.run(
        [sys.executable, monitor_pkg, "transfer", "send", "-l", local, "-p", remote],
        capture_output=True, text=True,
    )
    if r.returncode != 0:
        print(r.stderr, file=sys.stderr)
        sys.exit(1)

    output_lines = [l for l in r.stdout.splitlines() if l.strip()]
    for l in output_lines:
        print(l)


def main():
    ap = argparse.ArgumentParser(description="imgconv — convert images to 24-bit BMP for KSCOS")
    ap.add_argument("input", help="input image (jpg/png/...); relative paths also search tools/userdata/")
    ap.add_argument("-o", "--output", help=f"output BMP path (default: {_APPDATA_DIR}/<name>.bmp)")
    ap.add_argument("-s", "--size", help=f"resize to <width>x<height> (default: fit within {_MAX_W}x{_MAX_H})")
    ap.add_argument("-u", "--upload", metavar="REMOTE_PATH", help="upload to littlefs via transfer.py")
    args = ap.parse_args()

    cmd_convert(args)


if __name__ == "__main__":
    main()
