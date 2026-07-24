from __future__ import annotations

import json
import os
import subprocess
import sys
from typing import Optional

_PROJECT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
)
_PRESETS_FILE = os.path.join(_PROJECT_DIR, "CMakePresets.json")

_FIRMWARE_PRESETS = {
    "Firmware-Debug",
    "Firmware-Release",
}
_PC_PRESETS = {"PC Debug", "PC Release"}


def _load_presets() -> dict:
    with open(_PRESETS_FILE, encoding="utf-8") as f:
        return json.load(f)


def _resolve_binary_dir(preset_name: str) -> str:
    presets = _load_presets()
    for p in presets.get("configurePresets", []):
        if p["name"] == preset_name:
            return os.path.normpath(
                p["binaryDir"].replace("${sourceDir}", _PROJECT_DIR)
            )
    raise ValueError(f"Preset {preset_name!r} not found in CMakePresets.json")


def _exe_name(preset_name: str) -> str:
    if preset_name in _FIRMWARE_PRESETS:
        return "KSCOS.elf"
    return "KSCOS.exe"


def build(preset_name: str) -> str:
    binary_dir = _resolve_binary_dir(preset_name)
    exe_path = os.path.join(binary_dir, _exe_name(preset_name))

    cfg = subprocess.run(
        ["cmake", "--preset", preset_name],
        cwd=_PROJECT_DIR,
        capture_output=True, text=True,
    )
    if cfg.returncode != 0:
        print(cfg.stdout)
        print(cfg.stderr, file=sys.stderr)
        raise RuntimeError(f"cmake --preset {preset_name!r} failed")
    if cfg.stderr:
        print(cfg.stderr, file=sys.stderr)

    bld = subprocess.run(
        ["cmake", "--build", "--preset", preset_name],
        cwd=_PROJECT_DIR,
        capture_output=True, text=True,
    )
    print(bld.stdout)
    if bld.stderr:
        print(bld.stderr, file=sys.stderr)
    if bld.returncode != 0:
        print(bld.stderr, file=sys.stderr)
        raise RuntimeError(f"cmake --build --preset {preset_name!r} failed")

    if not os.path.isfile(exe_path):
        raise RuntimeError(
            f"Build completed but binary not found: {exe_path}"
        )
    return exe_path


def list_firmware_presets() -> list[str]:
    return sorted(_FIRMWARE_PRESETS)


def resolve_elf(preset_name: Optional[str] = None, elf_path: Optional[str] = None) -> str:
    if elf_path:
        return os.path.abspath(elf_path)
    if preset_name:
        return build(preset_name)
    raise ValueError("Either --preset or --elf must be provided")
