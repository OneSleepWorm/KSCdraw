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
                capture_output=True, timeout=5,
            )
            return proc.returncode == 0
        except (FileNotFoundError, subprocess.TimeoutExpired):
            return False

    @staticmethod
    def _run_cmd(cmd: list[str]) -> None:
        proc = subprocess.run(cmd, capture_output=True)
        if proc.stdout:
            sys.stdout.buffer.write(proc.stdout)
            sys.stdout.flush()
        if proc.returncode != 0:
            if proc.stderr:
                sys.stderr.buffer.write(proc.stderr)
                sys.stderr.flush()
            raise RuntimeError(f"pyOCD failed (rc={proc.returncode})")

    def flash(self, elf_path: str, *, verify: bool = True) -> None:
        cmd = [
            sys.executable, "-m", "pyocd", "flash",
            "--target", self.TARGET,
            elf_path,
        ]
        self._run_cmd(cmd)

    def reset(self) -> None:
        cmd = [
            sys.executable, "-m", "pyocd", "reset",
            "--target", self.TARGET,
        ]
        self._run_cmd(cmd)
