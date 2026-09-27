#!/usr/bin/env python3
"""Decode Nintendont's bounded Switch Pro ARM/PPC trace dump."""

import argparse
import struct
from pathlib import Path

MAGIC = 0x53505452
VERSION = 1
REGION_SIZE = 0x1000
EVENT_SIZE = 48
EVENT_OFFSET = 32
EVENT_NAMES = {
    1: "ARM_INIT",
    2: "ARM_PHASE",
    3: "ARM_TX_SUBCOMMAND",
    4: "ARM_RX_REPORT",
    5: "ARM_PARSE",
    6: "ARM_PUBLISH",
    7: "ARM_CHANNEL",
    8: "ARM_SELFTEST",
    0x100: "PPC_READ",
    0x101: "PPC_PAD",
}


def decode_region(blob: bytes, offset: int) -> list[str]:
    region = blob[offset : offset + REGION_SIZE]
    if len(region) != REGION_SIZE:
        raise ValueError("trace file must contain two complete 4096-byte regions")
    magic, version, writer, count, dropped, flags, _, _ = struct.unpack_from(
        ">8I", region, 0
    )
    if magic != MAGIC:
        raise ValueError(f"bad trace magic 0x{magic:08x} at offset 0x{offset:x}")
    if version != VERSION:
        raise ValueError(f"unsupported trace version {version}")
    lines = [
        f"writer={'ARM' if writer == 1 else 'PPC' if writer == 2 else writer} "
        f"count={count} dropped={dropped} flags=0x{flags:08x}"
    ]
    for index in range(min(count, 80)):
        values = struct.unpack_from(
            ">12I", region, EVENT_OFFSET + index * EVENT_SIZE
        )
        sequence, ticks, event_type, *data = values
        name = EVENT_NAMES.get(event_type, f"UNKNOWN_0x{event_type:x}")
        payload = " ".join(f"{value:08x}" for value in data)
        lines.append(f"{sequence:03d} {ticks:08x} {name:<18} {payload}")
    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    blob = args.trace.read_bytes()
    if len(blob) != REGION_SIZE * 2:
        raise SystemExit(
            f"expected {REGION_SIZE * 2} bytes, found {len(blob)} bytes"
        )
    for region_offset in (0, REGION_SIZE):
        for line in decode_region(blob, region_offset):
            print(line)
        print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
