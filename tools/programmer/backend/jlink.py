from __future__ import annotations

import subprocess
import sys

from .base import ProgrammerBackend


class JLinkBackend(ProgrammerBackend):
    name = "jlink"
    description = "SEGGER JLinkExe (JLink, JLink-OB)"

    DEVICE = "STM32F103C8"
    IFACE = "SWD"
    SPEED = 4000

    @classmethod
    def available(cls) -> bool:
        return cls._which("JLinkExe") is not None

    def _build_script(
        self, elf_path: str, action: str = "flash"
    ) -> str:
        lines = [
            f"device {self.DEVICE}",
            f"si {self.IFACE}",
            f"speed {self.SPEED}",
            "connect",
        ]
        if action == "flash":
            lines += [
                "erase",
                f"loadfile {elf_path}",
            ]
        lines += [
            "reset",
            "go",
            "exit",
        ]
        return "\n".join(lines)

    def _run(self, script: str) -> None:
        proc = subprocess.run(
            ["JLinkExe", "-autoconnect", "1"],
            input=script,
            capture_output=True,
        )
        if proc.stdout:
            sys.stdout.buffer.write(proc.stdout)
            sys.stdout.flush()
        if proc.returncode != 0:
            if proc.stderr:
                sys.stderr.buffer.write(proc.stderr)
                sys.stderr.flush()
            raise RuntimeError(f"JLinkExe failed (rc={proc.returncode})")

    def flash(self, elf_path: str, *, verify: bool = True) -> None:
        script = self._build_script(elf_path, action="flash")
        self._run(script)

    def reset(self) -> None:
        script = self._build_script("", action="reset")
        self._run(script)
