#!/usr/bin/env python3
"""
transfer.py — XMODEM file uploader to KSCOS

Connects to monitor.py daemon, sends file via XMODEM-128 protocol.

Usage:
  python transfer.py send -l local.bin -p /remote/path
  python transfer.py send -l local.bin -p /remote/path --port 12345
"""

import sys
import os
import time
import json
import socket
import struct
import argparse

DEFAULT_TCP_PORT = 12345
XMODEM_SOH = 0x01
XMODEM_EOT = 0x04
XMODEM_ACK = 0x06
XMODEM_NAK = 0x15
XMODEM_CAN = 0x18
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


def daemon_send(obj, tcp_port, timeout=TIMEOUT):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    sock.connect(("127.0.0.1", tcp_port))
    sock.sendall(json.dumps(obj).encode("utf-8"))
    resp = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        resp += chunk
        if b"\n" in resp:
            break
    sock.close()
    return json.loads(resp.split(b"\n", 1)[0].decode("utf-8"))


def xmodem_send(data: bytes, tcp_port: int) -> bool:
    seq = 1
    total = len(data)
    offset = 0

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
            resp = daemon_send({
                "cmd": "exchange",
                "data": bytes(packet).hex(),
                "hex": True,
                "noeol": True,
                "expect": "06",
                "timeout": TIMEOUT,
            }, tcp_port)
            received_hex = resp.get("received", "")
            if received_hex.endswith("06"):
                ok = True
                break
        if not ok:
            print(f"  Error: packet seq {seq} failed after {MAX_RETRIES} retries", file=sys.stderr)
            return False

        offset += PACKET_SIZE
        pct = int(offset / total * 100) if total else 100
        sys.stdout.write(f"\r  Sent seq {seq} ({pct}%)")
        sys.stdout.flush()
        seq = (seq + 1) & 0xFF

    for retry in range(MAX_RETRIES):
        resp = daemon_send({
            "cmd": "exchange",
            "data": bytes([XMODEM_EOT]).hex(),
            "hex": True,
            "noeol": True,
            "expect": "06",
            "timeout": TIMEOUT,
        }, tcp_port)
        received_hex = resp.get("received", "")
        if received_hex.endswith("06"):
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

    ping = daemon_send({"cmd": "ping"}, args.port)
    if ping.get("status") != "ok":
        print("Error: daemon not running", file=sys.stderr)
        sys.exit(1)

    resp = daemon_send({
        "cmd": "exchange",
        "data": f"transfer recv -p {remote_path} -n {file_size}",
        "hex": False,
        "noeol": False,
        "expect": "C",
        "timeout": 5.0,
    }, args.port)

    if resp.get("status") != "ok":
        print(f"Error: handshake failed: {resp}", file=sys.stderr)
        sys.exit(1)

    with open(local_path, "rb") as f:
        filedata = f.read()

    success = xmodem_send(filedata, args.port)
    if not success:
        sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description="transfer.py — XMODEM file upload to KSCOS")
    ap.add_argument("--port", type=int, default=DEFAULT_TCP_PORT, help="daemon TCP port")
    sub = ap.add_subparsers(dest="cmd")
    p_send = sub.add_parser("send")
    p_send.add_argument("-l", "--local", required=True, help="local file path")
    p_send.add_argument("-p", "--remote", required=True, help="remote path on littlefs")
    args = ap.parse_args()
    if args.cmd == "send":
        cmd_send(args)
    else:
        ap.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
