from __future__ import annotations

import subprocess
import sys

from .base import ProgrammerBackend


class STLinkBackend(ProgrammerBackend):
    name = "stlink"
    description = "STM32_Programmer_CLI (ST-Link)"

    @classmethod
    def available(cls) -> bool:
        return cls._which("STM32_Programmer_CLI") is not None

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
            raise RuntimeError(
                f"STM32_Programmer_CLI failed (rc={proc.returncode})"
            )

    def flash(self, elf_path: str, *, verify: bool = True) -> None:
        cmd = [
            "STM32_Programmer_CLI",
            "-c", "port=SWD",
            "-w", elf_path,
            "0x08000000",
            "-rst",
        ]
        self._run_cmd(cmd)

    def reset(self) -> None:
        cmd = [
            "STM32_Programmer_CLI",
            "-c", "port=SWD",
            "-rst",
        ]
        self._run_cmd(cmd)
