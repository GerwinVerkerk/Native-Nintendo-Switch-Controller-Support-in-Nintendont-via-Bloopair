# Minimal incoming Switch Pro support

This branch deliberately supports one connection path only:

1. Bloopair pairs an original Nintendo Switch Pro Controller.
2. Koopair exports the 48-byte pairing record to
   `sd:/wiiu/bloopair/nintendont-switch-pro.bin`.
   Pairing record version 2 stores the link key in the exact byte order used
   by an HCI Link Key Request Reply. Version 1 records are rejected.
3. The Nintendont loader validates the record and copies it to reserved shared
   memory.
4. The ARM kernel registers one dedicated, idempotent incoming HID listener for
   that controller address before the normal vWii listeners.
5. An A-wake connection is authenticated with the exported link key and then
   encrypted.
6. Nintendont accepts incoming HID control PSM `0x11` and interrupt PSM `0x13`
   in either order.  The controller is not exposed to the game until security
   and both channels are ready.
7. The first basic `0x3f` report is ignored.  Every following valid `0x3f`
   report is translated to the existing `C_CCP`/`BTPadCont` contract.

The minimal path does not perform inquiry, in-game pairing, outgoing HID,
remote-name detection, player LED control, rumble, SPI calibration, or report
mode `0x30` initialization.  Those features must not block basic input.

The listener pool reserves capacity for all ten existing vWii records plus the
one imported Switch Pro record.  Partial listener allocation is retried without
duplicating the control listener.

## Diagnostic status

During the game the kernel maintains a 128-byte status record containing only
booleans, result codes, counters, and the assigned GameCube channel.  It never
contains Bluetooth addresses or link-key bytes.  On a normal game exit or the
Nintendont exit combination it is written once to `switch-pro-minimal.bin` on
the active game device.  At the next Nintendont start, a USB copy is copied to
SD when both devices are mounted.

Decode it with:

```sh
python3 tools/decode_switch_pro_minimal.py switch-pro-minimal.bin
```
