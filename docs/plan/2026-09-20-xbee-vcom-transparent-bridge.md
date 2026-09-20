# XBee to VCOM transparent UART bridge

Status: Done (build and host-test verification; blocked on the Simplicity Studio baud
change below, and on-target checks not yet run)
Date: 2026-09-20
Related history: docs/history/2026-09-20-xbee-vcom-transparent-bridge.md

## Goal

A mode in which the MCU stops being a participant on the serial link and becomes
wire: every byte that arrives on VCOM is handed to the XBee UART unchanged, every
byte that arrives from the XBee is handed to VCOM unchanged, no byte is interpreted
and no byte is generated. With both EUSARTs at the same baud rate the link is then
equivalent to a direct serial connection, and XCTU can be pointed at the VCOM port as
if it were the module's own.

Two ways in, both in scope:

1. A `bridge` command in privileged EXEC mode of the existing CLI build, left by a
   silence-guarded `Ctrl-]` three times.
2. A dedicated `XBEE_APP_BRIDGE` value of the existing `XBEE_APP` build selector,
   which boots straight into the bridge with no CLI and no AT traffic.

## Context

Every route to the module today goes through the firmware. The `XBEE_APP_CLI` build
reads and writes AT parameters on request; the provisioning, bring-up and dump builds
each drive a fixed sequence. All of them speak through the `xbee` facade, which owns
the framing, Command mode entry and guard times.

That is the wrong shape for what a vendor tool does. XCTU needs to reach the module
itself: API frame exchanges, firmware updates, the profile and configuration editors,
its own terminal. None of that can be expressed through the AT facade, and
re-implementing it would be pointless work.

| Reused | From | What it gives the bridge |
| --- | --- | --- |
| `xbee_uart_*` | `src/drivers/inc/xbee_uart.h` | Byte in / byte out on EUSART0, with a 512-byte receive ring already fed from IRQ context |
| `console_uart_*` | `src/drivers/inc/console_uart.h` | Non-blocking reads from VCOM |
| `ring_buffer_t` | `src/utils/inc/ring_buffer.h` | The transmit ring towards the module |
| `xbee_power_*` | `src/drivers/inc/xbee_power.h` | The supply switch, for the dedicated build |
| `app_log_*` | `src/utils/inc/app_log.h` | Silenced, the way `cli_init()` already silences it |

## Background and references

- `docs/manuals/xbee_90002273_ref_manual.md`, lines 3044 to 3051 — Command mode guard
  times. The `+++` sequence and its guard windows belong to the module, not to the
  MCU, so `+++` must be forwarded verbatim. That is why the bridge escape uses a
  different character.
- `docs/manuals/efm32pg28_reference_manual.md`, section 20.2 (line 25940) and section
  20.3.2.6.1 (lines 26341 to 26377) — EUSART has separate 16-deep receive and
  transmit FIFOs, and a frame arriving while the receive FIFO is full is discarded
  with RXOF set. This sets the latency budget below.
- `docs/history/2026-09-18-eusart-flow-control-off.md` — RTS/CTS is off on both links.
  Neither side can throttle the other, so the timing budget has to close on buffering
  alone.
- `docs/history/2026-09-19-cmd-session-open-after-transparent-bringup.md` — the
  Command mode transport keeps a session open for `CT` after each command, which is
  why bridge entry from the CLI has to quiesce the facade first.

### Verified platform constraints

1. **The VCOM transmit path is a per-byte polled loop.**
   `platform/service/iostream/src/sl_iostream_uart.c:793-816` walks the buffer one
   character at a time through `uart_context->uart_periph->tx()`. The instance is
   configured with `async_tx_enabled = false`
   (`autogen/sl_iostream_init_eusart_instances.c`), so a write to VCOM blocks the
   super loop once the 16-deep transmit FIFO is full. The bridge therefore writes in
   bounded chunks.
2. **LF to CRLF insertion is on and happens inside that loop.**
   `SL_IOSTREAM_EUSART_VCOM_CONVERT_BY_DEFAULT_LF_TO_CRLF` is `1`
   (`config/sl_iostream_eusart_VCOM_config.h:76`), and the conversion is applied at
   `sl_iostream_uart.c:801-807`. Left on, every `0x0A` from the module would have a
   `0x0D` inserted in front of it, corrupting any XCTU API frame that contains
   `0x0A`. The flag is runtime-settable through
   `sl_iostream_uart_set_auto_cr_lf()` (`sl_iostream_uart.h:337`, wired at
   `sl_iostream_uart.c:188-189`), so no Studio change is needed for it.
3. **`sl_iostream_read()` never blocks in this project.** The blocking wait is gated
   on `SL_CATALOG_KERNEL_PRESENT` (`sl_iostream_uart.c:997-1001`) and there is no
   kernel, so the call drains the ring and reports `SL_STATUS_EMPTY`.
4. **The VCOM receive ring is 32 bytes.**
   `SL_IOSTREAM_EUSART_VCOM_RX_BUFFER_SIZE` is `32`
   (`config/sl_iostream_eusart_VCOM_config.h:71`), filled by circular LDMA. It is the
   smallest buffer in the system and the one the budget below turns on.
5. **`xbee_uart_write()` allows one transfer in flight, capped at 256 bytes**
   (`src/drivers/src/xbee_uart.c:215-273`). The bridge needs its own transmit ring
   and a chunked feed.

## Design

### Files

| File | Layer | Responsibility |
| --- | --- | --- |
| `src/app/inc/xbee_bridge.h` | app | Public API |
| `src/app/inc/xbee_bridge_config.h` | app | Build-time settings, all `#ifndef`-guarded |
| `src/app/src/xbee_bridge.c` | app | State machine and the two forwarding paths |
| `src/app/src/xbee_bridge_escape.h` | app, private | Escape detector interface, beside its `.c` as `cli_show.h` and `cli_value.h` already are |
| `src/app/src/xbee_bridge_escape.c` | app, private | Pure, time-injected escape detector. Host-tested |

Changed: `app.c`, `src/app/src/cli.c`, `src/drivers/inc/console_uart.h`,
`src/drivers/src/console_uart.c`, `cmake_gcc/CMakeLists.txt`, `test/CMakeLists.txt`.

### Driver additions

```c
/// Enter or leave binary mode: no LF to CRLF insertion on output.
sl_status_t console_uart_set_binary(bool binary);

/// Write bytes straight to the stream, bypassing stdio. Binary mode only.
sl_status_t console_uart_write_raw(const uint8_t *data, uint16_t len);
```

The file header of `console_uart.c` explains that output normally goes through stdio
so that `app_log`'s `printf` lines cannot be overtaken. In bridge mode logging is off
and nothing else prints, so there is nothing to interleave with, and the direct path
avoids newlib's line-buffer scan and a `fflush` per write.
`console_uart_set_binary(true)` flushes stdio on the way in so nothing is stranded in
the libc buffer.

### Interfaces

```c
/// How a bridge session is started.
typedef struct {
  bool allow_escape;    ///< Watch the terminal for the escape sequence.
  bool quiesce_facade;  ///< Drive xbee_process() until Command mode has closed.
} xbee_bridge_params_t;

/// What a bridge session did, for the report on the way out.
typedef struct {
  uint32_t to_module_bytes;
  uint32_t to_terminal_bytes;
  uint32_t dropped_bytes;    ///< Terminal bytes lost because the ring was full.
  uint32_t rx_overruns;      ///< Module bytes lost in the driver's ring.
} xbee_bridge_stats_t;

sl_status_t xbee_bridge_init(void);                       ///< Dedicated build.
void        xbee_bridge_process(void);                    ///< Both builds, every pass.
sl_status_t xbee_bridge_enter(const xbee_bridge_params_t *params);
sl_status_t xbee_bridge_leave(void);
bool        xbee_bridge_is_active(void);
sl_status_t xbee_bridge_get_stats(xbee_bridge_stats_t *stats);
```

The CLI passes `{ .allow_escape = true, .quiesce_facade = true }`; the dedicated build
passes `{ false, false }`, because there the facade was never initialised and there is
nowhere to return to.

### States

| State | What happens | Leaves on |
| --- | --- | --- |
| `OFF` | Nothing. | `xbee_bridge_enter()` or `xbee_bridge_init()` |
| `SETTLE` | Dedicated build only. `xbee_power_on()` has been called; waiting `XBEE_BRIDGE_SETTLE_MS` for the module to boot. | Deadline reached |
| `QUIESCE` | CLI entry only. `xbee_process()` is driven until `xbee_cmd_mode_is_ready()` is false, so XCTU does not meet a module already sitting in Command mode. Bounded by `XBEE_BRIDGE_QUIESCE_TIMEOUT_MS`. | Session closed, or the deadline |
| `ACTIVE` | Forwarding. | Escape detected, CLI only |

On the edge into `ACTIVE`: `app_log_set_level(APP_LOG_LEVEL_NONE)`,
`console_uart_set_binary(true)`, `xbee_uart_rx_flush()`, and the transmit ring is
cleared. `xbee_bridge_leave()` undoes the first two, restoring the log level that was
in force when the session started.

`xbee_uart_init()` is called on entry only in the dedicated build. In the CLI build
the facade already has its receive slots queued, and re-arming them would abort
operations under it.

### Forwarding

Per pass of `app_process_action()`, in this order:

1. **Drain VCOM** with `console_uart_read()` into a `XBEE_BRIDGE_VCOM_CHUNK` (32)
   byte stack buffer. First, because the VCOM receive ring is the smallest buffer in
   the system.
2. **Screen for the escape**, when `allow_escape`. Each byte goes through
   `xbee_bridge_escape_feed()`, which reports the bytes to forward. When
   `allow_escape` is false the bytes pass through untouched.
3. **Queue towards the module**: forwarded bytes are pushed into a
   `XBEE_BRIDGE_TX_RING_SIZE` (512) byte `ring_buffer_t`. A full ring drops the byte
   and bumps a counter; there is no flow control to push back with.
4. **Feed the XBee UART**: if `!xbee_uart_tx_busy()`, pop up to
   `XBEE_BRIDGE_XBEE_CHUNK` (64) bytes and `xbee_uart_write()` them.
5. **Drain the module**: `xbee_uart_read()` up to `XBEE_BRIDGE_VCOM_CHUNK` bytes and
   `console_uart_write_raw()` them.
6. **Tick the escape detector** with `xbee_bridge_escape_idle()`, which releases held
   bytes or reports the exit once the trailing guard has elapsed.

Time comes from `sl_sleeptimer_get_tick_count()` converted with
`sl_sleeptimer_tick_to_ms()`. No timer is allocated.

### The escape sequence

`Ctrl-]` (`0x1D`) three times, preceded and followed by at least
`XBEE_BRIDGE_ESCAPE_GUARD_MS` (1000) of silence, mirroring the module's own guard-time
discipline. `+++` is deliberately not used: it belongs to the module and has to reach
it.

Rules, implemented in `xbee_bridge_escape.c` as a pure state machine with the time
passed in:

- An escape character starts a candidate only when at least one guard time has passed
  since the previous byte. It is held back, not forwarded.
- Further escape characters increment the count. Any other byte flushes the held
  characters in order, then the new byte, and resets.
- On the third the detector arms and waits for another guard time of silence. A byte
  arriving inside that window flushes all three plus that byte.
- Silence completes: the bridge exits and nothing is released.
- Held characters that never complete a sequence are released by
  `xbee_bridge_escape_idle()` once the guard elapses.

Consequence, stated in the CLI banner: a genuine `0x1D` sent as data after an idle
line is delayed by up to one guard time. Every other byte, `+++` included, passes with
no added latency and no inspection.

### Link settings cross-check

`xbee_bridge.c` carries a compile-time check that
`SL_IOSTREAM_EUSART_VCOM_BAUDRATE` equals `SL_UARTDRV_EUSART_XBEE_BAUDRATE`, in the
spirit of the existing check in `src/app/src/xbee_provision.c:20-60`. A bridge between
two different rates silently loses data in the faster direction, and there is no flow
control to make that visible, so it is refused at build time rather than debugged on
hardware.

### CLI integration

- `cli_state_t` gains `CLI_STATE_BRIDGE`; the command identifiers gain `CMD_BRIDGE`.
- `root[]` gains, first in its alphabetical order:
  `{ "bridge", "Connect this terminal straight to the XBee module", CLI_NODE_KEYWORD, M_PRIV, CMD_BRIDGE, NULL, 0U }`.
  `ROOT_COUNT` is a `sizeof` expression, so no count needs bumping.
- `execute()` gains a `CMD_BRIDGE` arm: print the banner naming the escape, call
  `xbee_bridge_enter()`, set `state = CLI_STATE_BRIDGE` and return `true` so no prompt
  is printed.
- `cli_process()` gets an early branch, before `xbee_process()` and `service_input()`.
  Returning early is what makes the bridge exclusive: the facade is not driven and
  `service_input()` cannot steal bytes from the stream.
- `is_busy()` is left alone; `CLI_STATE_BRIDGE` never reaches `feed_byte()`.

### Application selection

`app.c` gains `#define XBEE_APP_BRIDGE 5`, the include, one `#elif` arm in each of
`app_init()` and `app_process_action()`, and the two `#error` strings are extended.

### Build registration

`cmake_gcc/CMakeLists.txt`, inside the marked sections only:

- `# Add additional sources here`: `../src/app/src/xbee_bridge.c` and
  `../src/app/src/xbee_bridge_escape.c`, alphabetically among the other app entries.
- No include-path change: `src/app/inc` is already in both
  `target_include_directories` blocks, and `app.c` only needs `xbee_bridge.h`, which
  lives there.
- `# Add additional macros here`: to build the dedicated bridge, set
  `XBEE_APP=XBEE_APP_BRIDGE` in **both** `target_compile_definitions` blocks, as the
  warning comment already in that file requires. The committed default stays
  `XBEE_APP_CLI`.

## Simplicity Studio changes required (PRIME RULE)

**Required. The build fails until this is done**, by design, through the cross-check
above.

1. Project Configurator → Software Components → Services → IO Stream →
   **IO Stream: EUSART** → instance **VCOM** → Configure →
   **Baud rate: `115200` → `9600`**. Save and let Studio regenerate.

   Studio rewrites `SL_IOSTREAM_EUSART_VCOM_BAUDRATE` in
   `config/sl_iostream_eusart_VCOM_config.h` and regenerates `autogen/`. Both are
   Studio-owned; nothing in this change edits them.

   After this, every terminal and XCTU connection to VCOM is 9600 8N1 with no flow
   control, the CLI console included. 9600 is also the XBee's factory-default `BD`
   (`BD=3`), so the bridge keeps working against a module that has been factory
   reset.

**Recommended, not required, and not acted on:**

2. Same editor → **Receive buffer size: `32` → `256`**
   (`SL_IOSTREAM_EUSART_VCOM_RX_BUFFER_SIZE`). Costs 224 bytes of RAM and widens the
   VCOM receive slack from 33 ms to 267 ms against a worst-case 17 ms stall in the
   polled transmit path. The budget below closes at 32 bytes with about twice the
   margin needed, so this is insurance rather than a fix. If it is taken,
   `CLI_READ_CHUNK` in `src/app/inc/cli_config.h` should be raised to match, as the
   two are deliberately equal today.

## Resource impact

- **RAM:** 512 bytes for the transmit ring, 32 for the VCOM chunk buffer, and about
  40 for the state and counters. Added to the CLI build as well, since the `bridge`
  command links the module in.
- **Flash:** roughly 1.5 to 2 KB for the two new sources and the CLI arm. Measured
  figures go in the history entry.
- **The dedicated `XBEE_APP_BRIDGE` build is much smaller than the CLI build.** It
  references only `xbee_bridge`, `xbee_uart`, `xbee_power`, `console_uart`,
  `app_log`, `ring_buffer` and the SDK, so `--gc-sections` drops the facade,
  `xbee_api`, `xbee_frame`, `xbee_cmd_mode`, the AT table, `cli`, `cli_parser` and
  `line_edit`.
- **Peripherals:** none added. No sleeptimer timer is allocated; the tick count is
  read directly.
- **Energy:** EM0 throughout, as now. `power_manager` is not installed, so `main.c`'s
  `sl_power_manager_sleep()` branch is already compiled out and the super loop
  free-runs. The bridge needs that: it is a polling design, and a bridge that slept
  would lose bytes it has no way to ask for again.

### Timing budget, 9600 8N1 both sides

1.042 ms per byte, 960 bytes per second each way.

| Path | Worst case | Slack | Margin |
| --- | --- | --- | --- |
| VCOM transmit stall, 32-byte write, 16 into the FIFO and 16 polled | 16.7 ms | 32-byte VCOM receive ring, 33.3 ms | 2x |
| XBee receive backlog while VCOM transmit stalls | 16 bytes | 512-byte ring, 533 ms | 32x |
| Transmit ring occupancy, one 64-byte transfer in flight for 66.7 ms, refilled at 960 bytes per second | 64 bytes | 512 bytes | 8x |

The budget closes because the rates are equal. It would not close at 115200 in and
9600 out, which is the substantive reason the baud change is required rather than
merely tidy.

## Risks and open questions

| Risk | Handling |
| --- | --- |
| A false escape drops the bridge mid-transfer | Guard times on both sides require one second of silence, exactly three `0x1D`, then one second of silence. Not reachable inside XCTU traffic. |
| VCOM receive overrun during the polled transmit stall | Budget closes with about twice the margin needed; recommendation 2 raises it to sixteen times. |
| Bytes dropped when the transmit ring fills | Counted, and printed on the way out of a CLI bridge session, so a lost byte is never silent. |
| XCTU meets a module still in Command mode | The `QUIESCE` state closes the session before forwarding starts. |
| The console becomes slower at 9600 | Accepted with the baud decision. `show xbee all` goes from roughly half a second to roughly six seconds. |

Open points, neither blocking:

- The dedicated `XBEE_APP_BRIDGE` build has no escape and no CLI, so the only way out
  is an MCU reset. That is what "the MCU is bypassed" implies, but it is a choice, not
  a constraint.
- `readme.md` is already stale: it documents VCOM at 115200 and names only
  `XBEE_APP_PROVISION` and `XBEE_APP_BRINGUP`, missing `DUMP` and `CLI`. This change
  does not touch it. Proposed, not done: update it to record 9600, all five `XBEE_APP`
  values and how to reach the bridge.

## Verification

### Host tests

```sh
cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure
```

Every existing suite must still pass, plus the new `test_xbee_bridge_escape`.

### Build checks

```sh
cd cmake_gcc && cmake --workflow --preset project
arm-none-eabi-size build/base/xbee_provision.out
```

Built twice: with `XBEE_APP=XBEE_APP_CLI`, the committed default, which must still
link and must carry the `bridge` command; and with `XBEE_APP=XBEE_APP_BRIDGE` in both
definition blocks. Warnings are treated as defects.

### On target

Requires the Studio baud change and a regeneration. Terminal and XCTU at 9600 8N1, no
flow control.

1. CLI build: the console comes up and `?` lists `bridge` in privileged mode only.
2. `enable`, then `bridge`: the banner prints and the prompt disappears.
3. XCTU on the same port discovers the module and reads its parameters.
4. Read, write and save a parameter in XCTU; reopen to confirm it stuck.
5. XCTU terminal tab: send `+++` and confirm `OK` comes back, proving the sequence is
   forwarded and not intercepted.
6. Plain terminal: pause, `Ctrl-]` three times, pause. `Bridge closed.` and the
   `xbee#` prompt return, with zero dropped bytes and zero overruns reported.
7. `Ctrl-]` once after a pause, then a data byte: both reach the module in order.
8. Flash the `XBEE_APP_BRIDGE` build. It boots silently and XCTU discovers the module
   with no console output anywhere in the stream.
9. An XCTU firmware update over the bridge: the longest sustained transfer available,
   and the real test of the buffering budget.
