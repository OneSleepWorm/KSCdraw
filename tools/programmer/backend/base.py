from __future__ import annotations

import abc
import shutil
from typing import Optional


class ProgrammerBackend(abc.ABC):
    name: str = ""
    description: str = ""

    @classmethod
    @abc.abstractmethod
    def available(cls) -> bool:
        ...

    @abc.abstractmethod
    def flash(self, elf_path: str, *, verify: bool = True) -> None:
        ...

    @abc.abstractmethod
    def reset(self) -> None:
        ...

    @staticmethod
    def _which(tool: str) -> Optional[str]:
        return shutil.which(tool)
