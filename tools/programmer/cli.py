from __future__ import annotations

import argparse
import sys

import builder
from backend import detect_backends, get_backend, list_backends


def _build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description="KSCOS Unified Programmer — build + flash for STM32/PC",
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    # ── flash ──
    p_flash = sub.add_parser("flash", help="Build (if needed) and flash firmware")
    p_flash.add_argument(
        "--preset",
        default="Firmware-Release",
        choices=builder.list_firmware_presets(),
        help="CMake preset (default: Firmware-Release)",
    )
    p_flash.add_argument("--elf", help="Path to existing .elf (skip build)")
    p_flash.add_argument(
        "--backend",
        choices=[b["name"] for b in list_backends()],
        help="Force a specific backend (default: auto-detect)",
    )
    p_flash.add_argument(
        "--interface",
        help="OpenOCD interface config name (e.g. cmsis-dap.cfg, default: stlink.cfg)",
    )
    p_flash.add_argument("--no-verify", action="store_true", help="Skip verify after flash")

    # ── reset ──
    p_reset = sub.add_parser("reset", help="Reset target")
    p_reset.add_argument(
        "--backend",
        choices=[b["name"] for b in list_backends()],
        help="Force a specific backend (default: auto-detect)",
    )
    p_reset.add_argument(
        "--interface",
        help="OpenOCD interface config name",
    )

    # ── list-backends ──
    sub.add_parser("list-backends", help="List available programmer backends")

    return ap


def _pick_backend(
    name: Optional[str], interface: Optional[str]
):
    if name:
        cls = get_backend(name)
        if not cls.available():
            sys.exit(f"Backend {name!r} is not available on this system")
        return cls

    available = detect_backends()
    if not available:
        sys.exit(
            "No programmer backend found.\n"
            "  Install one of: JLinkExe, OpenOCD, pyOCD (pip install pyocd), "
            "STM32_Programmer_CLI"
        )
    return _maybe_with_interface(available[0], interface)


def _maybe_with_interface(cls, interface: Optional[str]):
    from backend.openocd import OpenOCDBackend
    if issubclass(cls, OpenOCDBackend) and interface:
        return lambda: cls(interface=interface)
    return cls


def main():
    ap = _build_parser()
    args = ap.parse_args()

    if args.cmd == "list-backends":
        for b in list_backends():
            mark = "[ok]" if b["available"] else "[--]"
            print(f"  {mark} {b['name']:10s}  {b['description']}")
        return

    if args.cmd == "flash":
        elf = builder.resolve_elf(
            preset_name=args.preset,
            elf_path=args.elf,
        )
        backend_cls = _pick_backend(args.backend, args.interface)
        print(f"  target: {elf}")
        print(f"  backend: {backend_cls.name}")

        backend = backend_cls() if isinstance(backend_cls, type) else backend_cls()
        backend.flash(elf, verify=not args.no_verify)
        print("  done.")

    elif args.cmd == "reset":
        backend_cls = _pick_backend(args.backend, args.interface)
        print(f"  backend: {backend_cls.name}")
        backend = backend_cls() if isinstance(backend_cls, type) else backend_cls()
        backend.reset()
        print("  done.")


if __name__ == "__main__":
    main()
