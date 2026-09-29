# Native Switch Pro Bluetooth prototype

This branch adds a minimal native Bluetooth path for an original Nintendo
Switch 1 Pro Controller. It does not run Bloopair inside vWii and does not use a
USB adapter.

## What the code does

- Keeps the existing Wii Remote, Classic Controller and Wii U Pro paths intact.
- Reads link-key addresses already stored in the Wii U Bluetooth controller.
- Retains Nintendont's normal vWii `CONF_GetPadDevices` registrations.
- Listens for stored-key addresses that are absent from vWii SYSCONF and probes
  those devices as Switch Pro controllers.
- Runs a paced Switch initialization state machine outside the receive
  callback: wait 300 ms, request device information, set the player LED, enable
  vibration with silent frames, read user/factory stick calibration and then
  request continuous full `0x30` reports. Every stage requires its matching
  positive ACK before advancing.
- Retries each initialization command every 100 ms, up to ten sends, and uses
  safe default calibration only when no usable calibration block is returned.
- Parses both full `0x30`/`0x21` reports and fallback `0x3f` basic reports.
- Parses the standard `0x3f` HID button map and little-endian 16-bit axes used
  by original Switch Pro Controllers.
- Maps A/B/X/Y, D-pad, both sticks, L/R/ZL/ZR, Plus and Home through
  Nintendont's existing GameCube controller path.
- Caches the latest valid input report until Nintendont assigns a GameCube
  channel, then publishes it immediately instead of waiting for another packet.
- Uses Nintendo's neutral rumble frames in every subcommand and updates the
  controller player LED to the GameCube channel actually assigned.
- Publishes only the established `C_CCP` contract to the PPC/GameCube pad
  reader; the Switch-specific type bit remains private to the ARM state.
- Includes a one-shot half-second synthetic GameCube-A pulse after channel
  assignment plus a bounded RAM-only ARM/PPC trace. This tests the shared-memory
  boundary without file I/O during gameplay.
- Reopens the Bluetooth listeners after a Switch Pro disconnect and answers
  later link-key requests from the persisted controller key list.

## Connection directions

The controller can reach Nintendont through two distinct Bluetooth flows. They
share identity, security, Switch protocol and input translation only after the
HID channels are fully configured:

- **Incoming A-wake:** a previously bonded controller pages the console after a
  face-button press. Nintendont classifies the HCI Connection Request, accepts
  the ACL and the controller-initiated HID control/interrupt channels through
  one persistent pair of L2CAP listeners. It does not issue outbound HID
  connects on this path.
- **Outgoing SYNC/discovery:** inquiry finds a discoverable controller.
  Nintendont initiates the ACL, completes authentication/encryption, then opens
  HID control PSM `0x11` followed by interrupt PSM `0x13`.

Each Switch slot owns its connection origin, authentication/encryption flags,
link key, remote-name status and channel state. Listener registration is
idempotent so reconnects cannot consume additional listener PCBs. The two paths
converge only after security and both HID channels are ready; from there they
use the same paced Switch initialization and `C_CCP/BTPadCont` publication.

Class of Device `0x002508` is an early candidate filter. A successful remote
name of exactly `Pro Controller`, followed by the Switch Device Info response,
provides the stronger identity checks before normal input publication.

Rumble is intentionally not part of this first playable build. Capture and the
two stick-click buttons have no GameCube equivalents and are ignored. Minus is
reserved for a future mapping option.

## Pairing and handoff finding

Nintendont normally obtains device addresses from vWii SYSCONF in
`loader/source/main.c`, then registers only those addresses in `kernel/BT.c`.
Its lwBT stack separately obtains stored link keys from the Bluetooth controller
through the HCI `Read Stored Link Key` command.

Bloopair patches Wii U IOS-PAD. During pairing it calls
`registerNewDevice(...)` and `BTM_WriteStoredLinkKey(...)`. It does not copy a
controller entry into vWii SYSCONF. This branch bridges that missing device-list
and key handoff through a local versioned record exported by Koopair. The
loader validates the record and puts it in reserved shared memory. The kernel
validates it again, verifies that its console address matches the local
Bluetooth adapter, converts the conventional display-order controller address
to lwBT's raw HCI octet order, inserts the key in the software key cache and
creates one persistent incoming Switch listener even when the controller is
absent from vWii SYSCONF. Inquiry is disabled while this imported pairing is
active so it cannot starve an incoming A-wake page.

The record contains Bluetooth key material and must remain local to the Wii U
and SD card. Diagnostics expose only validation/result flags, never addresses
or key bytes. A successful build does not prove the hardware handoff; that
still requires an incoming reconnect and input test on the console.

Protocol reference: Bloopair commit
`a8b8aad07cf4df51c34e31e1694b3e2a64517de4`, especially
`ios/ios_pad/source/controllers/switch_controller.c`. Both projects use GPLv2
compatible licensing.

The security follow-up is based on two concrete host-stack paths rather than a
claim of hardware success. Bloopair retains the Wii U BTM security manager
(`ios/ios_pad/source/stack/btm_sec.c`), including authentication/encryption
requirements. BlueZ's classic HID host likewise raises a bonded control
connection to `BT_IO_SEC_MEDIUM` before using the interrupt channel
(`profiles/input/device.c`). Nintendont's lwBT already defined the relevant HCI
event/command numbers but previously ignored Authentication Complete and
Encryption Change and never sent Set Connection Encryption. The instrumented
build now exposes those two events separately. Whether the Wii U controller and
original Switch Pro accept this sequence remains a hardware-test question.

## Install on Wii U

1. Keep a copy of the currently working `sd:/apps/Nintendont/boot.dol`.
2. Install and boot Aroma with Bloopair as usual.
3. In the Wii U Menu, press the console SYNC button and the small SYNC button on
   the original Switch Pro Controller. Confirm that Bloopair can use it there.
4. Turn the Switch Pro Controller completely off before entering vWii, so the
   Wii U-side stack does not keep the active connection.
5. Copy this branch's `loader/loader.dol` to
   `sd:/apps/Nintendont/boot.dol`.
6. In USB Loader GX, keep the GameCube loader set to Nintendont and ensure its
   Nintendont path points to `sd:/apps/Nintendont/boot.dol`.
7. Start Nintendont directly once for the first test. After the game list or a
   game is visible, press a face button on the Switch Pro Controller to make it
   reconnect.

Rollback: restore the backed-up `boot.dol`. A SYNC/discovery test can create or
replace the controller link key, so Bloopair may need to pair the controller
again afterwards.

## Hardware test protocol

### Dual-route validation build

Exercise both connection directions in one game session. First press A on an
already bonded controller. This tests the incoming PAGE/listener route. If no
fixed player LED appears within 20 seconds, hold SYNC for 5-10 seconds instead;
this tests inquiry plus the outbound HID-host route in the same build. Once a
fixed player LED appears, test the face buttons, D-pad, both sticks, shoulders
and Plus once, then exit through Nintendont's normal game-exit combination.

The bounded trace records both candidate routes, origin, identity/security
events, L2CAP direction/results, Switch initialization, raw reports,
`BTPadCont` publication and PPC `PADStatus`. A normal game exit writes the
trace immediately, so the timed fallback wait is unnecessary. Reopen
Nintendont for 15 seconds without starting a game to copy a USB-volume trace to
SD before retrieving it.

### Instrumented pairing build

The pairing test build performs one Bluetooth inquiry when the in-game kernel
starts. Keep a Wii Remote awake during the test: its four player LEDs are used
as a diagnostic display. This deliberately overrides the Wii Remote's normal
player LED while the diagnostic is active.

| Visible Wii Remote LEDs | Phase | What the code has observed |
| --- | --- | --- |
| LED 1 solid | 1. Found | Inquiry returned a device with the original Switch Pro class of device `0x002508`; its Bluetooth address became the diagnostic target. |
| LEDs 1-2 solid | 2. SSP | A successful HCI Simple Pairing Complete event was received for that same address. |
| LEDs 1-3 solid | 3. Link key | A Link Key Notification for that address was received and the Wii U Bluetooth controller returned success for Write Stored Link Key. |
| LEDs 1-4 solid | 4. HID transport | Both HID L2CAP channels opened. For Switch Pro, the connection callback remains gated until authentication and encryption succeed. |
| LEDs 1+4 solid | Authentication requested | The Bluetooth controller accepted the HCI Authentication Requested command, but Authentication Complete has not succeeded yet. |
| LEDs 1+3 and 2+4 alternate | 5. Authenticated | HCI Authentication Complete succeeded for the target controller. |
| All four LEDs blink slowly | 6. Encrypted | HCI Encryption Change reported that link encryption is enabled. Only then does this build start HID protocol initialization. |
| All four LEDs blink at medium speed | 7. Protocol | The controller acknowledged HID Set Protocol (Report); Nintendont then waits 300 ms before starting the paced compatibility-mode initialization. |
| All four LEDs blink quickly | 8. Input | At least three continuous Switch Pro `0x30` or `0x3f` input reports were parsed. A `0x21` subcommand response no longer counts as streaming input. |
| LEDs 1+4 and 2+3 alternate | Authentication failed | HCI Authentication Complete returned a failure status. This is an error pattern, not a completed phase. |
| LEDs 1+2 and 3+4 alternate | Encryption failed | HCI Encryption Change failed or reported encryption disabled. This is an error pattern, not a completed phase. |

The display is cumulative: if events follow each other quickly, the later
numbered pattern proves all earlier numbered phases completed. If the display
stops at a solid pattern, record the highest number shown. No LEDs means the
target was not found (or the Wii Remote itself was not connected to the in-game
kernel).

Installation and launch:

1. Back up `sd:/apps/Nintendont/boot.dol`.
2. Copy the instrumented artifact's `boot.dol` to that exact path.
3. Start Nintendont directly from the vWii Homebrew Channel with a Wii Remote.
4. Start a GameCube game and keep the Wii Remote awake.
5. As the screen changes from Nintendont to the game, hold the small SYNC
   button on the original Switch 1 Pro Controller for several seconds.
6. Wait up to 30 seconds and record the highest Wii Remote LED phase plus the
   Switch Pro's own LED behavior. Do not infer success from compilation or from
   the Wii Remote LEDs beyond the exact phase meanings above.

Rollback: restore the backed-up `boot.dol`. A successful pairing may replace
the Switch Pro link key, in which case Bloopair may need to pair it again.

### Logging safety

Keep Nintendont's **Log** setting **Off** for controller tests. The general
Nintendont logger synchronously flushes each line to the active FAT volume.
Hardware testing of an earlier high-frequency trace build caused Double Dash
to show its generic disc error during startup; restoring the stable build and
turning Log off restored normal startup. The paced build removes that packet
trace and does not require file logging.

### Bounded ARM/PPC trace build

The boundary-trace build does not use Nintendont's general logger and requires
**Log = Off**. During play it records at most 80 fixed-size events per CPU in
two reserved RAM regions. ARM records security phases, Switch subcommands,
selected raw HID reports, parser output, channel assignment and `BTPadCont`
publication. PPC records changes observed in `BTPadCont` and the resulting
GameCube `PADStatus`.

Nothing is written during the 120-second capture window. At 120 seconds after
kernel startup, the kernel writes the two 4096-byte regions once
to `/switch-pro-trace.bin` on the active game volume. If that bounded write
fails, it retries at most twice at five-second intervals. A normal game exit
still performs the same one-shot write as a fallback, but is no longer required
to retrieve a useful trace.
Decode it on a development host with:

```sh
python3 tools/decode_switch_pro_trace.py switch-pro-trace.bin
```

For a useful capture, connect the Switch Pro, wait for its fixed player LED,
press A/B/X/Y separately and move both sticks through their range. Then wait at
least 150 seconds from the first connection attempt before powering the console
off. On the next boot, retrieve `/switch-pro-trace.bin` through FTP. The LED
diagnostics are not part of interpreting this trace.

Record pass/fail and any LED behavior for every step:

1. **Pairing persistence:** pair under Aroma/Bloopair, power the controller off,
   enter vWii/Nintendont, then power it on. Expected: one player LED becomes
   steady within 20 seconds.
2. **Face buttons:** verify A, B, X and Y individually in a known game or input
   test screen.
3. **D-pad:** verify all four directions, including diagonals.
4. **Left stick:** verify center, full range, both axes and absence of drift.
5. **Right stick/C-stick:** verify center, full range and both axes.
6. **Shoulders:** verify ZL/ZR as the GameCube analog L/R triggers; L acts as
   Nintendont's half-press modifier and R maps to GameCube Z, matching the
   existing Wii U Pro mapping.
7. **System buttons:** verify Plus maps to Start and Home exits through
   Nintendont's existing return path.
8. **Reconnect:** power the controller off during play, wait five seconds,
   power it on and verify input resumes without restarting Nintendont.
9. **Regression:** connect a Wii Remote + Classic Controller or Wii U Pro
   Controller and verify its existing input path still works.
10. **USB Loader GX:** repeat game launch through USB Loader GX after the direct
    Nintendont test succeeds.
11. **Return:** exit and confirm a clean return to the configured vWii/Wii U
    menu rather than a hang or black screen.

## Verification status

Verified without console hardware:

- Host parser/mapping/subcommand tests pass: `make -C tests clean all`.
- ARM/PPC source compiles and links to `loader/loader.dol`.
- Kernel link succeeds with the added code.

Observed on Wii U hardware:

- Builds `cceb3cf` and `f7213a0` reached phase 4: both HID L2CAP channels
  opened, while the Switch Pro LEDs continued their search pattern and no game
  input was received.
- Build `3d8b69c` also remained at phase 4. No successful Authentication
  Complete event was observed. Code review then found that Authentication
  Requested was issued at ACL connection time, before the freshly generated
  link key had been confirmed stored. The next build moves that request until
  after successful Write Stored Link Key completion and adds explicit
  authentication/encryption failure patterns. This sequencing correction is
  not hardware-validated yet.
- Build `29536c7` again remained at phase 4 with the Switch Pro LEDs sweeping
  and no input. The changed request timing alone therefore produced no visible
  improvement. The next build exposes Authentication Requested command status
  and answers a matching HCI Link Key Request with the fresh SSP key instead of
  unconditionally sending a negative reply. That change is not yet validated
  on Wii U hardware.
- Build `5872890` reached authenticated and encrypted HID transport, received a
  Set Protocol acknowledgement and at least one Switch report. Its fast LED
  diagnostic was a false positive for input because a `0x21` subcommand reply
  was accepted by the shared report parser. The Switch Pro player LEDs still
  swept and Double Dash received no input.
- Trace build `f33d5a7` with Nintendont Log enabled caused Double Dash's generic
  disc error and produced no usable trace file. It was rolled back. The paced
  build removes all `[SWTRACE]` packet logging and must be tested with Log off.
- Build `01266a8` eventually reached a fixed Switch Pro player LED and genuine
  streaming-report readiness, but no button or stick affected Double Dash on
  any tested player channel. Code review then found that its `0x3f` parser used
  the wrong face/shoulder bits and byte order. It also discarded the report
  that made the stream ready because channel assignment occurred in the next
  main-loop pass. Button mashing coincided with controller vibration; this
  follow-up therefore also replaces all-zero rumble payloads with the explicit
  neutral frames used by established Switch hosts. These corrections are not
  yet hardware-validated.
- Build `81780d3` still delivered no GameCube button or stick input. SYNC alone
  left the Switch Pro player LEDs sweeping; pressing A then caused a vibration
  and a fixed player-1 LED, but no control affected Double Dash. The fixed LED
  was not proof of GameCube channel assignment because initialization had
  already requested player LED 1. The next diagnostic build therefore requests
  all four Switch player LEDs while provisional, changes to one LED only after
  PADReadGC exposes a real channel, and mirrors live A/B/X/Y parsing on the Wii
  Remote LEDs. The input diagnostic is also delayed until a `BTPadCont` write
  actually occurs rather than merely parsing three reports.
- Build `239aa8f` reached a real assigned channel (fixed player LED) and at
  least one `BTPadCont` publication, but Double Dash still received no live
  button or stick input. This narrows the failure to live report values or the
  ARM-to-PPC/GameCube-pad boundary. The next build therefore uses the complete
  Bloopair-style `0x30` sequence and instruments both sides of that boundary in
  RAM.
- Trace 3 from build `8d226bd` proved that the one-shot inquiry completed with
  zero results before the controller became discoverable. Three existing vWii
  registrations/link keys were present, but none identified the Switch Pro;
  consequently no Switch target, HID, parser, publication or PPC events were
  produced. The discovery path now retries inquiry at bounded intervals (at
  most twelve attempts) until a Switch target is found, allowing the user to
  enter controller SYNC mode after the game has loaded.
- Trace 4 showed all bounded inquiries returning zero results. Source review
  then found that lwBT selected HCI Inquiry Mode `0x01` (results with RSSI,
  event `0x22`) while its event dispatcher only implemented standard Inquiry
  Result event `0x02`. The stack now selects Inquiry Mode `0x00`, matching its
  existing parser.
- The same trace also showed why an encrypted ACL link did not prove usable
  HID transport. Nintendont's Wii-family path only registered incoming L2CAP
  listeners. The Switch-only path now acts as a HID host: it initiates the ACL,
  waits for authentication and encryption, opens control PSM `0x11` outbound,
  waits for complete L2CAP configuration, then opens interrupt PSM `0x13`.
  Every ACL/L2CAP request and result/status is recorded in the bounded binary
  trace. Wii Remote and Wii U Pro keep the original incoming-listener route.
- The combined trace from build `53a5c32` found the real Switch class
  `0x002508` and reached an encrypted ACL link, but never issued an outgoing
  control-PSM request. It also exposed that every retry inquiry re-registered
  all SYSCONF controllers and replaced their BTE sockets while the connection
  was in progress. Inquiry reports additionally contained repeated addresses
  that consumed the bounded result list. The follow-up registers SYSCONF
  devices once, deduplicates inquiry results by address, reuses a single
  Switch slot, tracks pending/established ACL state and makes page timeout
  retryable. This combined correction is not yet hardware-validated.
- The follow-up trace from build `8a524e5` explained why the controller could
  wake with A yet never enter the Switch state machine: an already paired Pro
  Controller connects inbound and is not discoverable in inquiry. Later source
  comparison with BlueRetro and Bloopair found that this incoming PAGE route
  must accept controller-initiated HID channels; it must not reuse the outbound
  HID-open sequence. The current implementation therefore keeps explicit
  incoming and outgoing transport states, persistent incoming listeners and
  per-slot security/channel state. This split transport is host-tested and
  fully built, but not yet validated on Wii U hardware.

The current implementation keeps the proven authentication/encryption gate and
uses the full initialization sequence found in Bloopair and Linux
`hid-nintendo`: Device Info, player LED, vibration enable, user/factory stick
calibration and explicit continuous `0x30` report mode. It enforces
pacing/retries, accepts only matching positive ACKs, preserves the latest
report across channel assignment, uses silent subcommand rumble frames and
requires three genuine streaming reports before publishing a GameCube
controller. Command reply `0x21` can no longer claim a player slot.

Still requires Wii U hardware:

- Incoming A-wake reaching both controller-initiated HID channels.
- Outgoing SYNC/discovery opening control then interrupt from the host.
- Paced Device Info and player-LED initialization on an original controller.
- Continuous compatibility-mode `0x3f` reports and GameCube input.
- Real stick calibration/range behavior.
- Reconnection, Wii-controller regression and return-to-menu behavior.

Build used for the initial artifact:

- Base Nintendont commit: `bbc0a208e81d6ab0a828a431dc0b2ba6fc06aec5`
- devkitARM release 55 / GCC 10.2.0
- devkitPPC release 38 / GCC 10.2.0

Upstream recommends devkitARM r53-1 and devkitPPC r35-2. The linked archives
contain Windows executables, so this Linux build used the closest reproducible
devkitPro container available. That compiler difference is an additional item
for the hardware test, especially return-to-menu behavior.
