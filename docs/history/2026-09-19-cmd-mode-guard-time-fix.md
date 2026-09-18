# Fix: Command mode guard time ignored transmissions from the API transport

Date: 2026-09-19
Related plan: docs/plan/2026-09-18-xbee3-driver-stack.md

## Summary

Mode detection failed against a module in Transparent mode. The facade probed
with an API frame, got no answer as expected, fell back to the Command mode
escape sequence, and the module ignored that too, so bring-up reported that
neither path worked.

The cause was the guard time. `xbee_cmd_mode` measured the silence before the
escape sequence from its own transmissions only, so the API probe frame sent
moments earlier did not count. The sequence went out roughly 992 ms after the
probe finished on the wire, just under the 1000 ms the module requires, and the
module discarded it.

Fixed by moving the record of when the line last carried a byte into
`xbee_uart`, where every transport's traffic passes, and by adding a margin so
the silence is comfortably longer than the module's threshold rather than level
with it. Bring-up also now logs each step of the probe, which it previously did
not.

## The failure as reported

```
[     3.505] E bringup: module bring-up failed, status 0x001A, facade state 9
[     3.512] E bringup: serial link: 0 received, 11 sent, 0 overruns, 0 errors
[     3.519] E bringup: nothing received: check power, wiring and baud rate
```

Status `0x001A` is `SL_STATUS_NO_MORE_RESOURCE`, which the facade reports when
neither the API probe nor the escape sequence is answered. The 11 bytes sent are
the 8-byte AP query plus the three command characters, so both attempts did
leave the device. Nothing came back from either.

## Why the guard was short

Two defects compounded, and each alone was enough to break it.

- **The record was per transport.** `last_tx_tick` lived in `xbee_cmd_mode` and
  was only updated by that module's own writes. The API probe went out through
  `xbee_api` and then `xbee_uart_write()`, which `xbee_cmd_mode` never saw. When
  the facade called `xbee_cmd_mode_enter()`, the guard was measured from
  `xbee_cmd_mode_init()`, about two seconds in the past, so the deadline had
  already passed and the sequence was sent on the very next iteration.
- **The record was taken at the wrong moment.** `xbee_uart_write()` returns as
  soon as the transfer is handed to DMA, long before the last byte has been
  shifted out. At 9600 baud the 8-byte probe frame occupies the line for about
  8.3 ms after the call returns, so even within one transport the measured
  silence was short by that much.

With both in play the module saw about 992 ms of silence where the manual
requires a full guard time, `GT`, which defaults to 1000 ms (manual lines 3044
to 3051 and 6165 to 6173). The sequence was therefore not recognised, and the
module correctly stayed in Transparent mode passing the characters through as
data.

## Changes

| File | Change |
| --- | --- |
| `src/drivers/inc/xbee_uart.h`, `src/drivers/src/xbee_uart.c` | New `xbee_uart_last_tx_tick()`. The driver records the queue time plus the time the bytes take to shift out at the configured baud rate, so the value is when the line actually falls silent |
| `src/services/inc/xbee_cmd_mode_config.h` | New `XBEE_CMD_MODE_GUARD_MARGIN_MS`, default 50 |
| `src/services/src/xbee_cmd_mode.c` | Guard measured from the driver's record, plus the margin; the transport's own `last_tx_tick` removed |
| `src/services/src/xbee.c` | Bring-up now logs each probe step and the detected mode; the configured `probe_timeout_ms` is now actually applied, having previously been a field that did nothing |
| `test/fake_platform.c` | Models the shift-out time and provides the new accessor |
| `test/test_xbee_cmd_mode.c` | New regression test, plus updated guard expectations |

The baud rate is taken from `SL_UARTDRV_EUSART_XBEE_BAUDRATE` in the Studio
configuration, which is a read-only use of a generated macro.

## Regression test

`test_guard_counts_other_transport` reproduces the exact sequence: a frame is
written straight through the driver, as the API transport does, then Command mode
entry is requested. It asserts that the escape sequence is held back until a full
guard time plus the margin has passed since that frame finished transmitting.
Against the previous code the sequence went out immediately and the test fails.

## Simplicity Studio changes (made by the user)

None. This is application code only.

## Verification

**Build and host tests. The fix has not yet been confirmed against hardware.**

- Target build clean under `-Wall -Wextra -Os`. Image grew from 44 940 to 45 832
  bytes of text, the difference being the new bring-up log strings.
- Host tests: six suites, all passing, now including the regression test.
- Timing after the fix, with the defaults: the probe frame finishes at about
  1009 ms, the probe times out at 2000 ms, and the escape sequence goes out at
  2059 ms. That is 1050 ms of silence against the 1000 ms the module needs. The
  reply window then runs to about 3559 ms, and the module answers at about
  3059 ms.

## Known limitations and follow-ups

- **Still to be confirmed on hardware.** The next run should log the probe steps
  and end with the detected mode. If it still reports nothing received, the
  remaining suspects are the receive wiring and the module's baud rate, since
  neither the API probe nor the escape sequence would produce a reply then.
- The margin of 50 ms is a judgement, not a figure from the manual. It covers
  the tick conversion rounding and the estimate of when the last byte left. It
  can be reduced if bring-up time ever matters more than the safety margin.
- The shift-out time is calculated from the configured baud rate rather than
  measured. If the module and the instance are ever set to different rates, the
  estimate is wrong in the same direction as the mismatch.
- The facade still probes with an API frame first, which puts eight bytes of
  data on the air when the module is in Transparent mode. That is harmless but
  not free; probing Command mode first would avoid it at the cost of two guard
  times on every start-up in API mode.
