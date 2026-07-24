from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys

from .base import ProgrammerBackend


class STLinkUtilityBackend(ProgrammerBackend):
    name = "stutil"
    description = "ST-LINK Utility CLI (ST-LINK_CLI)"

    CLI = "ST-LINK_CLI.exe"
    FLASH_ADDR = "0x08000000"

    @classmethod
    def _config_path(cls) -> str:
        return os.path.join(os.path.dirname(__file__), "stutil.json")

    @classmethod
    def _ensure_config(cls) -> None:
        cfg = cls._config_path()
        if not os.path.isfile(cfg):
            os.makedirs(os.path.dirname(cfg), exist_ok=True)
            with open(cfg, "w", encoding="utf-8") as f:
                json.dump({"path": ""}, f, indent=4)

    @classmethod
    def _config_path_get(cls) -> str | None:
        cls._ensure_config()
        cfg = cls._config_path()
        try:
            with open(cfg, encoding="utf-8") as f:
                data = json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            return None
        p = data.get("path", "")
        if p and os.path.isdir(p):
            exe = os.path.join(p, cls.CLI)
            if os.path.isfile(exe):
                return exe
        return None

    @classmethod
    def _default_paths(cls) -> list[str]:
        paths = []
        for var in ("ProgramFiles(x86)", "ProgramFiles", "ProgramW6432"):
            root = os.environ.get(var)
            if root:
                p = os.path.join(root, "STMicroelectronics",
                                 "STM32 ST-LINK Utility",
                                 "ST-LINK Utility", cls.CLI)
                paths.append(p)
        return paths

    @classmethod
    def _find_cli(cls) -> str | None:
        found = shutil.which(cls.CLI)
        if found:
            return found

        cfg = cls._config_path_get()
        if cfg:
            return cfg

        env = os.environ.get("ST_LINK_CLI")
        if env and os.path.isfile(env):
            return env

        for p in cls._default_paths():
            if os.path.isfile(p):
                return p
        return None

    @classmethod
    def available(cls) -> bool:
        return cls._find_cli() is not None

    def _cli(self) -> str:
        cli = self._find_cli()
        if cli is None:
            cfg = self._config_path()
            raise RuntimeError(
                "ST-LINK_CLI.exe not found.\n"
                f"  Edit {cfg} and set \"path\" to your "
                "ST-LINK Utility directory,\n"
                "  or add it to PATH, or set ST_LINK_CLI env var."
            )
        return cli

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
            raise RuntimeError(f"ST-LINK_CLI failed (rc={proc.returncode})")

    def flash(self, elf_path: str, *, verify: bool = True) -> None:
        bin_path = elf_path.replace(".elf", ".bin")
        if not os.path.isfile(bin_path):
            raise RuntimeError(
                f"Binary not found: {bin_path}. "
                "Run objcopy first or rebuild."
            )
        cmd = [
            self._cli(), "-c", "SWD",
            "-P", bin_path, self.FLASH_ADDR,
            "-Rst",
        ]
        if verify:
            cmd += ["-V", "after_programming"]
        self._run_cmd(cmd)

    def reset(self) -> None:
        self._run_cmd([self._cli(), "-c", "SWD", "-Rst"])
