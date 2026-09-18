# XBee 3 driver stack, phase 5: facade and bring-up

Date: 2026-09-18
Related plan: docs/plan/2026-09-18-xbee3-driver-stack.md

## Summary

Added `xbee`, the mode-independent facade the application talks to, and
`xbee_bringup`, the first module that uses it. The facade powers the module,
works out which serial mode it is in, reads the parameters bring-up needs, and
then routes every request to whichever transport applies. `app.c` now calls
bring-up, so the firmware exercises the whole stack from the super loop.

This completes the five phases of the plan. The stack builds clean and its
hardware-independent parts are covered by 27 398 host assertions, but **no part
of it has been run on hardware yet**.

## Motivation

The point of the stack is that provisioning logic should not care whether the
module is in Transparent mode or either API mode. The facade is where that
promise is kept: one set of calls, one request type, one place that knows how
each mode reaches a parameter.

## Changes

| File | Change |
| --- | --- |
| `src/services/inc/xbee.h`, `xbee_config.h`, `src/services/src/xbee.c` | New: bring-up state machine, mode detection, request routing, data and status callbacks, mode switching, hardware reset, sleep control |
| `src/app/inc/xbee_bringup.h`, `src/app/src/xbee_bringup.c` | New: power the module, wait for the facade, log what it found |
| `app.c` | Calls bring-up from `app_init()` and `app_process_action()` |
| `cmake_gcc/CMakeLists.txt` | Registered both sources, added the application include path, restored the `slc` include block |
| `src/app/inc/.gitkeep`, `src/app/src/.gitkeep` | Removed: the folder now holds real files |
| `readme.md` | Updated the module list |

Design points worth recording:

- **The mode probe costs one frame.** An AP query and its answer contain no byte
  that needs escaping in either direction, so a single unescaped frame is
  understood whether the module is in API mode 1 or API mode 2, and the value it
  returns says which (manual lines 7188 to 7245). Only if that goes unanswered
  does the facade fall back to the escape sequence, which costs two guard times.
- **A module that claims API mode but does not answer one is reported, not
  guessed at.** If the escape sequence succeeds and AP turns out to be 1 or 2,
  bring-up fails with the observed value rather than picking a mode.
- **A mode change takes effect locally only once the module acknowledges it.**
  Switching sooner would leave the transports talking past the module.
- **Bring-up tolerates a parameter it cannot read.** A value the module declines
  to report is left at its default rather than failing the whole sequence, since
  it may simply not apply to that variant.
- **Sleep is driven by the status line in both directions.** Entering waits for
  the line to fall, waking waits for it to rise, and both have timeouts. After a
  wake, requests are refused for a short settling period, which stands in for the
  manual's rule about waiting after CTS falls (lines 4726 to 4729), because CTS
  is not wired on this board. The facade also notices the module waking by
  itself, as it does at the end of a cyclic sleep period.
- **Sleep is refused unless it can work.** The facade reads SM, D8 and D9 during
  bring-up and returns not-supported unless the module is configured for pin
  sleep, so a caller never waits on a line the module is ignoring. It also
  refuses while a request is in flight or a Command mode session is open, which
  the manual lists as conditions that prevent sleep (lines 4789 to 4811).
- **One application request at a time.** Command mode is serial by nature and
  provisioning issues one command at a time, so allowing more would add states
  without buying anything.

## Simplicity Studio changes (made by the user)

None. This phase is application code only.

## Verification

**Build only, plus the host tests from earlier phases. Nothing has been run on
hardware.**

- Target build: clean, no warnings under `-Wall -Wextra -Os`.
- Image size, `arm-none-eabi-size` on `cmake_gcc/build/base/xbee_provision.out`:

  | | text | data | bss |
  | --- | --- | --- | --- |
  | Before phase 5 | 24164 | 372 | 261772 |
  | After phase 5 | 44940 | 476 | 261668 |

  The image grew by 20 776 bytes of text once the stack was finally referenced
  and stopped being discarded by `--gc-sections`. About 4.6 kB of that is the
  newlib printf family that `app_log` pulls in, which the earlier logging work
  already measured.

- Per-module footprint across the whole stack:

  | Module | text | bss |
  | --- | --- | --- |
  | `ring_buffer` | 240 | 0 |
  | `byte_util` | 652 | 0 |
  | `xbee_power` | 86 | 0 |
  | `xbee_sleep` | 152 | 0 |
  | `xbee_reset` | 214 | 5 |
  | `xbee_uart` | 588 | 800 |
  | `xbee_frame` | 6102 | 0 |
  | `xbee_at_table` | 3252 | 0 |
  | `xbee_api` | 1353 | 855 |
  | `xbee_cmd_mode` | 1866 | 317 |
  | `xbee` | 3494 | 458 |
  | `xbee_bringup` | 1285 | 1 |
  | Total | 19 284 | 2436 |

  Against the plan's estimates the static RAM is almost exactly as predicted,
  2436 bytes against 2500. Flash is higher, 19.3 kB against 14 kB, mostly in the
  frame codec and the facade. Both are comfortable against the part's 256 kB of
  RAM and 1 MB of flash.

- Host tests: six suites, all passing, 27 398 assertions in total.

  | Suite | Checks |
  | --- | --- |
  | `test_ring_buffer` | 3101 |
  | `test_byte_util` | 70 |
  | `test_xbee_frame` | 340 |
  | `test_xbee_at_table` | 23298 |
  | `test_xbee_api` | 186 |
  | `test_xbee_cmd_mode` | 403 |

- Static review across the stack: every `sl_status_t` and `Ecode_t` return value
  is checked; no dynamic allocation anywhere; no blocking wait, every wait is a
  state machine with a timeout; interrupt-shared state is `volatile` and read
  inside a core atomic section where more than one word is involved; every copy
  is bounded by an explicit length check; deadlines are compared as signed
  differences so they survive the tick counter wrap.
- `git status` shows no Studio-owned file modified.

## Known limitations and follow-ups

- **Nothing has been run on hardware.** The whole of the plan's on-target
  verification is outstanding, for every phase. Until then, no claim is made
  that any of this works against a real module. The checks to run are:
  - the control pins: PA00 high after power on, a 10 ms low pulse on PB03,
    PB05 following the sleep request, PB04 reading high with a powered module;
  - bring-up detecting each of AP=0, 1 and 2 correctly, and the log showing the
    serial number, firmware and hardware versions;
  - `xbee_set_mode()` round-tripping between all three modes;
  - the serial link reporting no overruns after a session;
  - `xbee_hw_reset()` producing the boot status frame and a successful re-probe;
  - with SM set to 1, entering sleep driving the status line low and exiting
    driving it high, after which the next command is answered.
- The facade has no host test suite of its own. Its two dependencies are already
  faked, so one could be added; it was left out to keep this phase to the plan's
  scope, and the on-target checks above cover the same ground more convincingly.
- `XBEE_POWER_SETTLE_MS` and `XBEE_RESET_PULSE_MS` are both unverified. The boot
  time after power-up and after a reset, and the minimum reset low time, are in
  the XBee 3 Hardware Reference Manual, which is not in `docs/manuals/`. Adding
  it would let both be cited rather than guessed.
- The facade drives both transports while the mode is still being detected. Each
  ignores traffic that is not its own, but it does mean a module emitting
  Transparent data during the probe has those bytes consumed by the API parser.
- Bring-up reads twelve parameters one at a time, so it takes twelve round trips
  after the mode is known. At 9600 baud that is well under a second, but it could
  be shortened later if provisioning time matters.
- `xbee_send_data()` in Transparent mode refuses a named destination, because the
  module sends to whatever DH and DL hold. A caller that wants a specific
  destination in that mode must set those parameters first.
