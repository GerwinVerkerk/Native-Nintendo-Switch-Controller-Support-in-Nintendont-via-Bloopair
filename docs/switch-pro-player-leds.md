# Switch Pro player LEDs

Baseline: `fec2335` (`fix(bt): reserve imported Switch listener`). The
pairing, security, L2CAP, initialization and input-publication paths remain
unchanged except for replacing the fixed player-LED payload and scheduling an
LED-only update after channel changes.

## Primary-source mapping

- Linux `hid-nintendo` commit `6f8319e3`, `hid-nintendo.c`: subcommand `0x30`
  sets player lights. Its Nintendo-sourced patterns for players 1 through 4
  are `{1,0,0,0}`, `{1,1,0,0}`, `{1,1,1,0}`, and `{1,1,1,1}`. Packed into the
  low nibble, the masks are `0x01`, `0x03`, `0x07`, and `0x0f`.
- Bloopair commit `a8b8aad`, `switch_controller.c:setPlayerLeds()`: sends the
  supplied low-nibble LED value unchanged as Switch subcommand `0x30` (except
  for the documented right-Joy-Con physical-order reversal).
- Bloopair `controllers.c:ledMaskToPlayerNum()` interprets Wii-style one-hot
  masks separately. That conversion is not used by Switch Pro
  `setPlayerLeds()`, so it does not justify deriving a native Switch player
  pattern as `1 << channel`.

The explicit Nintendont mapping is therefore:

| GameCube channel | Player | Switch mask | Visible steady LEDs |
| ---: | ---: | ---: | --- |
| 0 | 1 | `0x01` | 1 |
| 1 | 2 | `0x03` | 1+2 |
| 2 | 3 | `0x07` | 1+2+3 |
| 3 | 4 | `0x0f` | 1+2+3+4 |

## Stateflow

1. `BTUpdateRegisters()` computes the definitive free GameCube channel.
2. Only a channel change calls `SwitchProIncomingSetChannel()`.
3. During the existing initialization sequence, the `0x30` entry takes the
   current mapped mask instead of a fixed byte.
4. After initialization, a changed desired mask schedules exactly one LED
   subcommand through the existing report counter, timeout, retry, and `0x21`
   ACK path.
5. The acknowledged sent mask becomes the applied mask. If the desired channel
   changed while an ACK was outstanding, one subsequent update is scheduled.
6. Normal `0x30` input reports do not schedule or send LED commands.
7. Disconnect resets LED state; reconnect and channel assignment therefore
   apply the current channel again.

Status version 5 records `channel`, `led_desired_mask`, `led_sent_mask`,
`led_acks`, and `led_send_attempts` for the bounded hardware tests.
