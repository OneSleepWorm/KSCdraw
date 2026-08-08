"""transfer_client.py — XMODEM-128 file upload (monitor subcommand).

Invoked as: python KSCOS/tools/monitor transfer send -l <local> -p <remote>
"""
from __future__ import annotations

import os
import sys
import time

from client import _send_to_daemon

XMODEM_SOH = 0x01
XMODEM_EOT = 0x04
XMODEM_ACK = 0x06
PACKET_SIZE = 128
MAX_RETRIES = 10
TIMEOUT = 5.0


def crc16_ccitt(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
        crc &= 0xFFFF
    return crc


def xmodem_send(data: bytes, tcp_port: int) -> bool:
    seq = 1
    total = len(data)
    offset = 0
    last_bucket = -1

    while offset < total:
        chunk = data[offset:offset + PACKET_SIZE]
        chunk = chunk.ljust(PACKET_SIZE, b'\x1a')
        packet = bytearray()
        packet.append(XMODEM_SOH)
        packet.append(seq & 0xFF)
        packet.append((~seq) & 0xFF)
        packet.extend(chunk)
        crc = crc16_ccitt(bytes(packet[3:]))
        packet.append((crc >> 8) & 0xFF)
        packet.append(crc & 0xFF)

        ok = False
        for retry in range(MAX_RETRIES):
            resp = _send_to_daemon({
                "cmd": "exchange",
                "data": bytes(packet).hex(),
                "hex": True,
                "noeol": True,
                "expect": "06",
                "timeout": TIMEOUT,
            }, tcp_port)
            received_hex = resp.get("received", "")
            if "06" in received_hex:
                ok = True
                break
        if not ok:
            print(f"  Error: packet seq {seq} failed after {MAX_RETRIES} retries", file=sys.stderr)
            return False

        offset += PACKET_SIZE
        pct = int(offset / total * 100) if total else 100
        bucket = pct // 5
        if bucket > last_bucket:
            last_bucket = bucket
            sys.stdout.write(f"\r  Sent seq {seq} ({bucket * 5}%)")
            sys.stdout.flush()
        seq = (seq + 1) & 0xFF

    for retry in range(MAX_RETRIES):
        resp = _send_to_daemon({
            "cmd": "exchange",
            "data": bytes([XMODEM_EOT]).hex(),
            "hex": True,
            "noeol": True,
            "expect": "06",
            "timeout": TIMEOUT,
        }, tcp_port)
        received_hex = resp.get("received", "")
        if "06" in received_hex:
            print("\n  Done.")
            return True

    print("\n  Error: EOT not acknowledged", file=sys.stderr)
    return False


def cmd_send(args):
    local_path = args.local
    remote_path = args.remote

    if not os.path.isfile(local_path):
        print(f"Error: local file not found: {local_path}", file=sys.stderr)
        sys.exit(1)

    file_size = os.path.getsize(local_path)
    print(f"Uploading: {local_path} ({file_size} bytes)")
    print(f"  Remote: {remote_path}")

    ping = _send_to_daemon({"cmd": "ping"}, args.tcp_port, timeout=1.0)
    if ping.get("status") != "ok":
        print("Error: daemon not running", file=sys.stderr)
        sys.exit(1)

    resp = _send_to_daemon({
        "cmd": "exchange",
        "data": f"transfer recv -p {remote_path} -n {file_size}",
        "hex": False,
        "noeol": False,
        "expect": "C",
        "timeout": 5.0,
    }, args.tcp_port)

    if resp.get("status") != "ok":
        print(f"Error: handshake failed: {resp}", file=sys.stderr)
        sys.exit(1)

    with open(local_path, "rb") as f:
        filedata = f.read()

    success = xmodem_send(filedata, args.tcp_port)
    if not success:
        sys.exit(1)
