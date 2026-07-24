from __future__ import annotations

import subprocess
import sys
from typing import Optional

from .base import ProgrammerBackend


class STLinkBackend(ProgrammerBackend):
    name = "stlink"
    description = "STM32_Programmer_CLI (ST-Link)"

    @classmethod
    def available(cls) -> bool:
        return cls._which("STM32_Programmer_CLI") is not None

    def flash(self, elf_path: str, *, verify: bool = True) -> None:
        cmd = [
            "STM32_Programmer_CLI",
            "-c", "port=SWD",
            "-w", elf_path,
            "0x08000000",
            "-rst",
        ]
        print(f"  STM32_Programmer_CLI: {' '.join(cmd)}")
        proc = subprocess.run(cmd, capture_output=True, text=True)
        print(proc.stdout)
        if proc.returncode != 0:
            print(proc.stderr, file=sys.stderr)
            raise RuntimeError(
                f"STM32_Programmer_CLI failed (rc={proc.returncode})"
            )

    def reset(self) -> None:
        cmd = [
            "STM32_Programmer_CLI",
            "-c", "port=SWD",
            "-rst",
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        print(proc.stdout)
        if proc.returncode != 0:
            print(proc.stderr, file=sys.stderr)
            raise RuntimeError(
                f"STM32_Programmer_CLI failed (rc={proc.returncode})"
            )
