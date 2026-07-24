from __future__ import annotations

import os
import subprocess
import sys
from typing import Optional

from .base import ProgrammerBackend


_CFG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cfg")


def _ocd_path(p: str) -> str:
    return p.replace("\\", "/")


class OpenOCDBackend(ProgrammerBackend):
    name = "openocd"
    description = "OpenOCD (JLink, ST-Link, DAPLink, CMSIS-DAP, FTDI, ...)"

    DEFAULT_INTERFACE = "stlink.cfg"

    def __init__(self, interface: Optional[str] = None):
        self.interface = interface or self.DEFAULT_INTERFACE

    @classmethod
    def available(cls) -> bool:
        return cls._which("openocd") is not None

    def _config_path(self) -> str:
        return _ocd_path(os.path.join(_CFG_DIR, "stm32f103c8.cfg"))

    def flash(self, elf_path: str, *, verify: bool = True) -> None:
        cmds = [
            "program",
            _ocd_path(elf_path),
            "0x08000000",
        ]
        if verify:
            cmds.append("verify")
        cmds += ["reset", "exit"]

        cmd = [
            "openocd",
            "-f", f"interface/{self.interface}",
            "-f", self._config_path(),
            "-c", " ".join(cmds),
        ]
        print(f"  openocd: {' '.join(cmd)}")
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.stdout:
            print(proc.stdout)
        if proc.stderr:
            print(proc.stderr, file=sys.stderr)
        if proc.returncode != 0:
            combined = (proc.stdout or "") + (proc.stderr or "")
            if verify and "Programming Finished" in combined:
                print("  warning: verify failed, but programming succeeded. "
                      "Use --no-verify to skip verify.")
            else:
                raise RuntimeError(f"OpenOCD failed (rc={proc.returncode})")

    def reset(self) -> None:
        cmd = [
            "openocd",
            "-f", f"interface/{self.interface}",
            "-f", self._config_path(),
            "-c", "init",
            "-c", "reset run",
            "-c", "exit",
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.stdout:
            print(proc.stdout)
        if proc.stderr:
            print(proc.stderr, file=sys.stderr)
        if proc.returncode != 0:
            raise RuntimeError(f"OpenOCD failed (rc={proc.returncode})")
