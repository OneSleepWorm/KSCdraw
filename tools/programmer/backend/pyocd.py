from __future__ import annotations

import subprocess
import sys

from .base import ProgrammerBackend


class PyOCDBackend(ProgrammerBackend):
    name = "pyocd"
    description = "pyOCD (DAPLink, ST-Link, JLink CMSIS-DAP, ...)"

    TARGET = "stm32f103c8"

    @classmethod
    def available(cls) -> bool:
        try:
            proc = subprocess.run(
                [sys.executable, "-m", "pyocd", "--version"],
                capture_output=True, text=True, timeout=5,
            )
            return proc.returncode == 0
        except (FileNotFoundError, subprocess.TimeoutExpired):
            return False

    def flash(self, elf_path: str, *, verify: bool = True) -> None:
        cmd = [
            sys.executable, "-m", "pyocd", "flash",
            "--target", self.TARGET,
            elf_path,
        ]
        if verify:
            cmd.append("--verify")
        print(f"  pyocd: {' '.join(cmd)}")
        proc = subprocess.run(cmd, capture_output=True, text=True)
        print(proc.stdout)
        if proc.returncode != 0:
            print(proc.stderr, file=sys.stderr)
            raise RuntimeError(f"pyOCD failed (rc={proc.returncode})")

    def reset(self) -> None:
        cmd = [
            sys.executable, "-m", "pyocd", "reset",
            "--target", self.TARGET,
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        print(proc.stdout)
        if proc.returncode != 0:
            print(proc.stderr, file=sys.stderr)
            raise RuntimeError(f"pyOCD failed (rc={proc.returncode})")
