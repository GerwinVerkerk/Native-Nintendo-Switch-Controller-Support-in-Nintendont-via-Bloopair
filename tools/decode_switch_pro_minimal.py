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
    "transport_ready", "transport_timeout", "control_dedicated",
    "interrupt_dedicated", "duplicate_listener",
)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} switch-pro-minimal.bin", file=sys.stderr)
        return 2
    data = pathlib.Path(sys.argv[1]).read_bytes()
    if len(data) not in (128, 144, 192, 256):
        raise SystemExit(f"unexpected size: {len(data)}")
    values = struct.unpack(">IHHIi" + "I" * ((len(data) - 16) // 4), data)
    magic, version, size, flags, last_error = values[:5]
    expected_sizes = {3: 128, 4: 128, 5: 144, 6: 192, 7: 256}
    if (magic != 0x53504D53 or version not in expected_sizes or
            size != len(data) or expected_sizes[version] != size):
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
        *(('stored_address_matches', 'control_channel_owner',
           'interrupt_channel_owner') if version >= 4 else
          ('reserved_0', 'reserved_1', 'reserved_2')),
        *(("led_desired_mask", "led_sent_mask", "led_acks",
           "led_send_attempts") if version >= 5 else ()),
        *(("slot_count", "slot_channel_0", "slot_channel_1",
           "slot_channel_2", "slot_channel_3", "slot_init_acks_0",
           "slot_init_acks_1", "slot_init_acks_2", "slot_init_acks_3",
           "slot_published_mask", "slot_connected_mask", "reserved")
          if version >= 6 else ()),
        *(("slot_raw_left_y_0", "slot_raw_left_y_1",
           "slot_raw_left_y_2", "slot_raw_left_y_3",
           "slot_raw_left_y_min_0", "slot_raw_left_y_min_1",
           "slot_raw_left_y_min_2", "slot_raw_left_y_min_3",
           "slot_raw_left_y_max_0", "slot_raw_left_y_max_1",
           "slot_raw_left_y_max_2", "slot_raw_left_y_max_3",
           "slot_published_left_y_0", "slot_published_left_y_1",
           "slot_published_left_y_2", "slot_published_left_y_3")
          if version >= 7 else ()),
    )
    print(f"flags=0x{flags:08x} last_error={last_error}")
    for bit, name in enumerate(FLAGS):
        print(f"{name}={int(bool(flags & (1 << bit)))}")
    for name, value in zip(names, values[5:]):
        if (name == "init_last_send_result" or
                name.startswith("slot_published_left_y_")) and value & 0x80000000:
            value -= 0x100000000
        print(f"{name}={value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
