#!/usr/bin/env python3
import pathlib
import struct
import sys

FLAGS = (
    "pairing_valid", "listener_ready", "acl_connected", "key_replied",
    "auth_requested", "authenticated", "encrypt_requested", "encrypted",
    "control_open", "interrupt_open", "connected", "basic_seen",
    "basic_parsed", "published", "init_started", "device_info",
    "report_mode", "init_complete", "full_seen", "full_parsed",
    "init_retried", "init_failed", "transport_pending",
    "transport_ready", "transport_timeout",
)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} switch-pro-minimal.bin", file=sys.stderr)
        return 2
    data = pathlib.Path(sys.argv[1]).read_bytes()
    if len(data) != 128:
        raise SystemExit(f"unexpected size: {len(data)}")
    values = struct.unpack(">IHHIi17I11I", data)
    magic, version, size, flags, last_error = values[:5]
    if magic != 0x53504D53 or version != 3 or size != 128:
        raise SystemExit("invalid status header")
    names = (
        "listener_result", "acl_count", "key_requests", "auth_result",
        "encrypt_result", "control_count", "interrupt_count",
        "basic_reports", "parsed_reports", "publishes", "channel",
        "command_reports", "init_sent", "init_acks", "init_retries",
        "full_reports", "init_index",
        "bte_state", "control_l2cap_state", "interrupt_l2cap_state",
        "transport_checks", "transport_deferred", "init_send_attempts",
        "init_last_send_result", "transport_timeouts",
    )
    print(f"flags=0x{flags:08x} last_error={last_error}")
    for bit, name in enumerate(FLAGS):
        print(f"{name}={int(bool(flags & (1 << bit)))}")
    for name, value in zip(names, values[5:30]):
        if name == "init_last_send_result" and value & 0x80000000:
            value -= 0x100000000
        print(f"{name}={value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
