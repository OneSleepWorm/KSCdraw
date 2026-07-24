from .base import ProgrammerBackend
from .jlink import JLinkBackend
from .openocd import OpenOCDBackend
from .pyocd import PyOCDBackend
from .stlink import STLinkBackend
from .stutil import STLinkUtilityBackend

_BACKENDS = [
    PyOCDBackend,
    STLinkUtilityBackend,
    OpenOCDBackend,
    JLinkBackend,
    STLinkBackend,
]

_BACKEND_MAP = {b.name: b for b in _BACKENDS}


def detect_backends() -> list[type[ProgrammerBackend]]:
    available = []
    for cls in _BACKENDS:
        try:
            if cls.available():
                available.append(cls)
        except Exception:
            pass
    return available


def get_backend(name: str) -> type[ProgrammerBackend]:
    cls = _BACKEND_MAP.get(name)
    if cls is None:
        raise ValueError(
            f"Unknown backend {name!r}. "
            f"Available: {', '.join(sorted(_BACKEND_MAP))}"
        )
    return cls


def list_backends() -> list[dict]:
    result = []
    for cls in _BACKENDS:
        try:
            avail = cls.available()
        except Exception:
            avail = False
        result.append({
            "name": cls.name,
            "description": cls.description,
            "available": avail,
        })
    return result


__all__ = [
    "ProgrammerBackend",
    "detect_backends",
    "get_backend",
    "list_backends",
    "_BACKENDS",
]
