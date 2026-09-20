# Transparent UART bridge between the XBee module and VCOM

Date: 2026-09-20
Related plan: docs/plan/2026-09-20-xbee-vcom-transparent-bridge.md

## Summary

A mode in which the MCU stops participating on the serial link and forwards bytes
between the UARTDRV `XBEE` instance (EUSART0) and the iostream `VCOM` instance
(EUSART2) without interpreting any of them. Reachable two ways: a `bridge` command in
privileged EXEC mode of the console build, left by a silence-guarded `Ctrl-]` three
times; and a new `XBEE_APP_BRIDGE` value of the `XBEE_APP` build selector that boots
straight into it with no console and no AT traffic.

**This change does not build until the Simplicity Studio change below has been made.**
That is deliberate, not an oversight: see "Why the build is broken on purpose".

## Motivation

Every route to the module went through the firmware, and all of them through the
`xbee` facade, which owns the framing, Command mode entry and guard times. That is the
wrong shape for what a vendor tool does. XCTU needs the module itself for API frame
exchanges, firmware updates, its profile and configuration editors and its own
terminal, and none of that can be expressed through the AT facade.

## Changes

### New files

| File | Layer | What it does |
| --- | --- | --- |
| `src/app/inc/xbee_bridge.h` | app | Public API: `xbee_bridge_init`/`_process` for the dedicated build, `_enter`/`_leave`/`_is_active`/`_get_stats` for the console |
| `src/app/inc/xbee_bridge_config.h` | app | Build-time settings: ring and chunk sizes, the escape character, count and guard time, the settle and quiesce timeouts |
| `src/app/src/xbee_bridge.c` | app | The four-state machine and both forwarding paths |
| `src/app/src/xbee_bridge_escape.h` | app, private | Escape detector interface, beside its `.c` as `cli_show.h` and `cli_value.h` already are |
| `src/app/src/xbee_bridge_escape.c` | app, private | The detector. Pure, time passed in, no I/O |
| `test/test_xbee_bridge_escape.c` | test | Eleven cases over the detector |

### Changed files

- `src/drivers/inc/console_uart.h`, `src/drivers/src/console_uart.c` — two functions,
  `console_uart_set_binary()` and `console_uart_write_raw()`, and one new static
  (`binary_mode`). Binary mode turns off the instance's LF to CRLF insertion through
  `sl_iostream_uart_set_auto_cr_lf()` and flushes stdio on the way in.
  `console_uart_write_raw()` refuses to run outside binary mode.
- `src/app/src/cli.c` — `CLI_STATE_BRIDGE`, `CMD_BRIDGE`, one node at the head of
  `root[]`, `cmd_bridge()` and `finish_bridge()`, one `execute()` arm, and an early
  branch at the top of `cli_process()`.
- `app.c` — `XBEE_APP_BRIDGE 5`, the include, one arm in each of `app_init()` and
  `app_process_action()`, and both `#error` strings extended.
- `cmake_gcc/CMakeLists.txt` — the two new sources under `# Add additional sources
  here`, and the `XBEE_APP` comment in the `# Add additional macros here` section
  extended to describe the new value. No include path changed: `src/app/inc` was
  already in both `target_include_directories` blocks. The committed value stays
  `XBEE_APP_CLI`.
- `test/CMakeLists.txt` — registers the new suite, with `src/app/inc` and
  `src/app/src` on its include path.

### Decisions worth recording

**The bridge is exclusive, and `cli_process()` enforces it by returning early.** The
facade and the bridge would otherwise drain the same receive ring, and
`service_input()` would eat bytes that belong to the module. Rather than teach either
of them about the other, the console simply does not run while the bridge holds the
link: the bridge branch is the first thing in `cli_process()` and it returns.

**The console keeps the transport; the dedicated build takes it.** `xbee_uart_init()`
re-arms the driver's queued receive operations, which would abort the ones the facade
is waiting on. So `xbee_bridge_params_t` carries `own_transport`, false when entering
from the console and true in the dedicated build, which also applies the supply and
waits `XBEE_BRIDGE_SETTLE_MS` for the module to boot.

**Entering from the console closes any Command mode session first.** The Command mode
transport keeps a session open for `CT` after each command
(`docs/history/2026-09-19-cmd-session-open-after-transparent-bringup.md`), so a
console that has just read a parameter leaves the module sitting in Command mode and a
tool connecting through the bridge would have its first data answered with `ERROR`.
The `QUIESCE` state drives `xbee_process()` and sends `xbee_cmd_mode_exit()` once,
bounded by `XBEE_BRIDGE_QUIESCE_TIMEOUT_MS`. On timeout it abandons the session and
forwards anyway: a bridge that refuses to open is worse than one that opens with the
module in a state the tool can recover from itself.

**The escape is `Ctrl-]`, not `+++`.** The module's own escape sequence has to reach
it, so the bridge cannot claim it. `0x1D` is the telnet escape and the obvious
alternative.

**Nothing is discarded, so the detector is safe on a binary stream.** It holds
candidate characters rather than forwarding them, and releases them in arrival order
the moment the sequence is broken or the guard expires. The only cost on a stream that
never means to escape is that an escape character following an idle line is delayed by
up to one guard time. A false trigger would need one second of silence, exactly three
`0x1D`, then another second of silence, which is not reachable inside back-to-back
frame traffic.

**Time is a parameter of the detector, not something it reads.** That is what makes
every guard window boundary reachable from a host test rather than only from a board
with a stopwatch, and it is the same split `cli_value.c` already uses.

**The bridge's millisecond clock converts the whole elapsed tick count, not a delta.**
A super-loop pass is far shorter than one 32768 Hz tick, so `sl_sleeptimer_tick_to_ms()`
on a per-pass delta truncates every one of them to zero and the clock never moves. The
elapsed count is taken modulo the counter, so the counter's own wrap is handled; the
derived millisecond value wraps after about 36 hours of continuous bridging, which the
guard comparison reads as "not elapsed" and which costs at worst one retry of the
escape sequence.

**Output goes straight to the stream, which the console deliberately does not do.**
`console_uart.c`'s header explains that console output uses stdio so `app_log`'s
`printf` lines cannot be overtaken. In bridge mode the log is silenced and nothing else
prints, so there is nothing to interleave with, and the direct path avoids newlib's
line-buffer scan and a `fflush` per write. `console_uart_write_raw()` returns
`SL_STATUS_INVALID_STATE` outside binary mode so that the invariant is enforced rather
than documented.

**Dropped bytes are counted and reported.** Neither link has flow control, so a full
ring means a lost byte and nothing else can be done about it. The console prints the
byte counts, drops and driver overruns when the bridge closes, so a loss is never
silent.

**The console warns that what it knows about the module may be stale.** Whatever was
on the other end had the module to itself and may have changed its serial mode, so
`finish_bridge()` says so and points at `reload` rather than pretending the cached
identity still holds.

### Why the build is broken on purpose

`xbee_bridge.c` carries a compile-time check that `SL_IOSTREAM_EUSART_VCOM_BAUDRATE`
equals `SL_UARTDRV_EUSART_XBEE_BAUDRATE`, in the spirit of the existing check in
`src/app/src/xbee_provision.c`. They are 115200 and 9600 today, so the build stops with
that `#error` until the Studio change below is made.

A bridge between two different rates loses data in the faster direction as soon as a
transfer outlasts the slower side's buffer, and neither link has flow control
(`docs/history/2026-09-18-eusart-flow-control-off.md`), so the loss would surface as
corrupted frames rather than as an error. Refusing it at build time is cheaper than
debugging it on hardware.

## Simplicity Studio changes (made by the user)

**None yet. Required before this change can build:**

Project Configurator → Software Components → Services → IO Stream →
**IO Stream: EUSART** → instance **VCOM** → Configure →
**Baud rate: `115200` → `9600`**. Save and let Studio regenerate.

That rewrites `SL_IOSTREAM_EUSART_VCOM_BAUDRATE` in
`config/sl_iostream_eusart_VCOM_config.h` and regenerates `autogen/`. Nothing in this
change touches either.

Afterwards every terminal and XCTU connection to VCOM is 9600 8N1 with no flow
control, the console included. 9600 was chosen over raising the XBee side to 115200
because it is the module's factory-default `BD` (`BD=3`), so the bridge keeps working
against a module that has been factory reset, and because raising the XBee side would
also have required changing `XBEE_PROV_BD` and setting `BD` on the module first.

**Recommended, not made:** same editor, **Receive buffer size `32` → `256`**
(`SL_IOSTREAM_EUSART_VCOM_RX_BUFFER_SIZE`). Costs 224 bytes of RAM and widens the
receive slack from 33 ms to 267 ms against a worst-case 17 ms stall in the polled
transmit path. The budget closes at 32 bytes with about twice the margin needed, so
this is insurance rather than a fix. If it is taken, `CLI_READ_CHUNK` in
`src/app/inc/cli_config.h` should be raised to match, as the two are deliberately
equal today.

## Verification

Build only, host tests and static review. **Nothing here has been run on hardware**,
and it cannot be until the Studio change above is made.

- **Host tests: 13 binaries, all passing**, up from 12. The new
  `test_xbee_bridge_escape` covers eleven cases: plain traffic passing through
  untouched; `+++` forwarded verbatim; the full sequence with both guards exiting
  silently; no leading guard meaning it is data however many are sent; a run broken by
  data released in arrival order; a byte inside the trailing window making the whole
  sequence payload again; an incomplete run released on idle; a fourth character in
  the trailing window; a second attempt succeeding after a failed one; an escape sent
  immediately on entry being data; and NULL arguments refused.

- **Both variants build with no warnings** under the project's flags. The figures
  below were taken with the baud cross-check temporarily bypassed, since the tree as
  committed does not compile until Studio has been run; the bypass was reverted
  immediately and the committed file carries the check.

  | Build | `.text` | Change |
  | --- | --- | --- |
  | `XBEE_APP_CLI` before this change | 60 380 | — |
  | `XBEE_APP_CLI` with the `bridge` command | 62 268 | +1 888 |
  | `XBEE_APP_BRIDGE` | 49 164 | 13 104 below the console build |

  The dedicated build is the smallest variant in the project. It references only
  `xbee_bridge`, `xbee_uart`, `xbee_power`, `console_uart`, `app_log`, `ring_buffer`
  and the SDK, so `--gc-sections` drops the facade, `xbee_api`, `xbee_frame`,
  `xbee_cmd_mode`, the AT table, `cli`, `cli_parser` and `line_edit`.

- **RAM** has to be read per object, not from the linker's `.bss` total: the heap is
  sized as whatever RAM is left (`heap_size = heap_limit - __HeapBase` in
  `autogen/linkerfile.ld`) and counts as `.bss`, so the total is pinned to the part's
  RAM and moves only by alignment. Per object: `xbee_bridge.c` 566 bytes (the 512-byte
  transmit ring plus state and counters), `xbee_bridge_escape.c` 0, `console_uart.c`
  4 → 5 for the new flag.

- **Timing budget, static, at 9600 8N1 both sides** (1.042 ms per byte, 960 bytes per
  second each way): a 32-byte write to the terminal stalls the loop about 16.7 ms once
  the 16-deep FIFO is full, against 33.3 ms of slack in the 32-byte receive ring, so
  about twice the margin needed. The module's 512-byte receive ring covers 533 ms
  against that same stall. The transmit ring holds 512 bytes against one 64-byte
  transfer in flight for 66.7 ms. The budget closes because the rates are equal, which
  is the substantive reason the Studio change is required rather than merely tidy.

## Known limitations and follow-ups

- **Nothing is verified on target.** The on-target sequence is listed in the plan's
  Verification section and still has to be run: console banner, XCTU discovery, a
  parameter round trip, `+++` reaching the module, the escape returning the prompt, a
  held `Ctrl-]` reaching the module in order, the dedicated build booting silently, and
  an XCTU firmware update as the real test of the buffering budget.
- The dedicated `XBEE_APP_BRIDGE` build has no escape and no console, so the only way
  out is an MCU reset. That follows from what the build is for, but it is a choice.
- `readme.md` is stale and this change did not touch it: it documents VCOM at 115200
  and names only `XBEE_APP_PROVISION` and `XBEE_APP_BRINGUP`, missing `DUMP`, `CLI` and
  now `BRIDGE`. Proposed, not done.
- The console runs at 9600 after the Studio change. `show xbee all` goes from roughly
  half a second to roughly six seconds. Accepted with the baud decision.

---

# Fix: the dedicated bridge build never opened the terminal

Date: 2026-09-20
Related plan: docs/plan/2026-09-20-xbee-vcom-transparent-bridge.md
Corrects: the entry above, in the same file

## Summary

The `XBEE_APP_BRIDGE` build forwarded nothing in either direction. Reported from
hardware as "typing +++ into VCOM gets no response".

`xbee_bridge_enter()` never called `console_uart_init()`. The console build reaches the
bridge through `cli_init()`, which calls it first, so the console path worked and the
defect was invisible there; the dedicated build has no console and nothing else calls
it. `console_stream` therefore stayed NULL, `console_uart_read()` returned
`SL_STATUS_NOT_INITIALIZED` on every pass and `service_terminal()` returned without
reading a byte, while `console_uart_write_raw()` failed the same way in the other
direction. The bridge ran its state machine correctly and moved no data.

This is a defect the build could not have caught and the host tests do not reach: the
escape detector is pure, and nothing in the suite exercises module wiring.

## Changes

| File | Change |
| --- | --- |
| `src/app/src/xbee_bridge.c` | `xbee_bridge_enter()` calls `console_uart_init()` before anything else and fails the session if it cannot bind the stream |
| `src/app/src/xbee_bridge.c` | The log is silenced in `xbee_bridge_enter()` rather than in `begin_active()`, after every fallible setup step |
| `src/drivers/src/console_uart.c`, `src/drivers/inc/console_uart.h` | `console_uart_set_binary()` applies the mode even when the stdio flush fails, and reports the flush result |

### Why the bridge now binds the terminal itself

The first version assumed a caller had. That assumption held in one of the two builds
and silently failed in the other, which is the worst shape for it: nothing returned an
error the caller looked at, because the forwarding helpers deliberately swallow their
status to keep the loop quiet. Binding it in `xbee_bridge_enter()` makes the module
responsible for what it needs, and the session is refused outright if the stream cannot
be had.

### Two other silent-death paths of the same family, closed

**A failed flush no longer leaves the output direction dead.**
`console_uart_set_binary(true)` used to return early if the stdio flush failed, leaving
`binary_mode` false, which makes every later `console_uart_write_raw()` return
`SL_STATUS_INVALID_STATE`. The module-to-terminal direction would have gone quiet with
nothing to show for it. The mode change itself cannot fail, so it is now always
applied and the flush result is reported rather than acted on.

**The log is silenced before the settling and quiescing steps, not after them.**
`advance_quiesce()` drives `xbee_process()`, which logs, and those lines would have
landed in a stream the operator had already been told was transparent. Silencing moved
to `xbee_bridge_enter()`, placed after every step that can still fail so that a refused
session leaves the caller with a log to read.

## Simplicity Studio changes (made by the user)

`IO Stream: EUSART` instance `VCOM`, baud rate 115200 to 9600, as the entry above
required. Confirmed in `config/sl_iostream_eusart_VCOM_config.h`:
`SL_IOSTREAM_EUSART_VCOM_BAUDRATE` is now `9600`, matching
`SL_UARTDRV_EUSART_XBEE_BAUDRATE`. The cross-check in `xbee_bridge.c` passes and both
variants build.

`SL_IOSTREAM_EUSART_VCOM_RX_BUFFER_SIZE` is still `32`; the recommendation to raise it
was not taken, which the budget allows.

## Verification

Build and host tests only. The on-target check that prompted this fix has not been
re-run yet.

- Host tests: 13 binaries, all passing. None of them cover this defect; see the
  follow-up below.
- Both variants build with no warnings, now with the real baud cross-check in force
  rather than bypassed: `XBEE_APP_CLI` `.text` 62 268, `XBEE_APP_BRIDGE` `.text`
  49 196 (32 bytes more than before this fix).

## Known limitations and follow-ups

- **Not yet confirmed on hardware.** The reported symptom has a second possible cause
  that this fix does not address: `+++` only enters Command mode when the module is in
  Transparent mode. `XBEE_PROV_AP` is `1`, so if the module has been provisioned it is
  in API mode 1, where three `0x2B` bytes are not a frame and are discarded without a
  reply. A terminal typing `+++` is therefore a weak test of the bridge; XCTU, which
  speaks API natively, is the real one.
- **No test covers the wiring between the bridge and its transports.** The escape
  detector is host-tested because it is pure; `xbee_bridge.c` is not, because it calls
  the drivers directly. `test/fake_platform.c` already stands in for `xbee_uart` in the
  transport suites, so a fake `console_uart` beside it would make this class of defect
  reachable from the host. Proposed, not done.

---

# Fix: the dedicated bridge build left two module control lines floating

Date: 2026-09-20
Related plan: docs/plan/2026-09-20-xbee-vcom-transparent-bridge.md
Corrects: the two entries above, in the same file

## Summary

After the console-transport fix, the `bridge` command in the console build worked and
the `XBEE_APP_BRIDGE` build still did not: XCTU found nothing at the same baud rate and
framing that the console build had just been talking at.

`xbee_bridge_enter()` set up two of the module's four control lines. The facade's
`xbee_init()` does all four, in this order: `xbee_power_init()`, `xbee_reset_init()`,
`xbee_sleep_init()`, `xbee_uart_init()` (`src/services/src/xbee.c`). The bridge did the
first and the last, so `XBEE_nRESET` (PB03) and `XBEE_SLEEPRQ` (PB05) were left in the
MCU's own reset state, which is disabled and floating, and the module reads both
through its internal pull-ups.

`XBEE_SLEEPRQ` is the one that bites. Floating, it reads high, which is a sleep
request. A module that has `SM` set to a pin sleep mode goes to sleep and answers
nothing at all, which looks exactly like a bridge that does not forward. It never
showed in the console build because `cli_init()` calls `xbee_init()`, which drives that
line low before anything else happens.

## Changes

| File | Change |
| --- | --- |
| `src/app/src/xbee_bridge.c` | `xbee_bridge_enter()` calls `xbee_reset_init()` and `xbee_sleep_init()` in the `own_transport` branch, in the same order and between the same two steps as `xbee_init()` |
| `src/app/src/xbee_bridge.c` | Includes `xbee_reset.h` and `xbee_sleep.h` |

`xbee_reset_init()` puts PB03 in open drain with the output register high, releasing
the line so the module's pull-up holds it out of reset. `xbee_sleep_init()` drives PB05
low, so the module stays awake whatever `SM` it is configured for, and sets
`XBEE_nSLEEP_Status` (PB04) as a plain input.

### The general shape of both defects so far

Both were the same mistake in different clothes: the dedicated build was written as the
console path minus the console, and what it quietly inherited from `cli_init()` was not
enumerated. First the terminal stream, now two GPIOs. The bridge now does its own
hardware setup end to end when `own_transport` is set, and the order matches
`xbee_init()` so the two paths can be compared line by line.

No reset pulse is issued, as planned: the build applies the supply, waits
`XBEE_BRIDGE_SETTLE_MS` and forwards, leaving the module in whatever state a tool finds
it.

## Simplicity Studio changes (made by the user)

None. All four pins were already reserved and named in Pin Tool, and
`config/pin_config.h` is unchanged.

## Verification

Build only. **Not yet confirmed on hardware**, and the hypothesis that a floating
`XBEE_SLEEPRQ` was what silenced the module is reasoned from the pin state and the
module's pull-up, not measured. What is certain is that the two builds set up different
hardware, and that they no longer do.

- `XBEE_APP_BRIDGE` builds with no warnings: `.text` 49 292, 96 bytes more than before
  this fix.
- Host tests unaffected: 13 binaries, all passing. None of them reach this code.

## Known limitations and follow-ups

- If XCTU still finds nothing, the remaining difference between the two builds is time,
  not wiring: the console build has had the module powered and settled for as long as
  the session has been open, while this build gives it `XBEE_BRIDGE_SETTLE_MS`
  (1000 ms) after applying the supply. `xbee_power_init()` drives the supply low and
  `xbee_power_on()` raises it microseconds later, which is too short to be a real power
  cycle, so a module that was already running does not reboot and a module that was off
  gets the full second. Worth measuring PB05 and PA00 on a scope before changing
  anything further.
- The proposed host-side fake for `console_uart` would not have caught this one either.
  A fake for the control lines already exists, `test/fake_drivers.c`, used by the
  facade suite; a bridge suite built on both would cover the whole setup sequence.
  Still proposed, still not done.
