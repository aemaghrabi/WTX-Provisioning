# Cisco-style CLI console over VCOM

Date: 2026-09-20
Related plan: docs/plan/2026-09-20-cli-console.md

## Summary

Added a fourth application, `XBEE_APP_CLI`, and made it the default build. It serves an interactive
console on the VCOM link with Cisco IOS command syntax: three modes, `?` context help, `show xbee
at <NAME>` and `show xbee all` to read parameters, and `xbee at <NAME> <VALUE>` in a
password-protected configuration mode to write them.

Six new modules, spread across the layers, plus three new host test binaries. The three existing
applications are untouched and still build byte for byte identically.

## Motivation

Changing one AT parameter on a board meant editing `xbee_provision_config.h`, rebuilding and
reflashing, or attaching XCTU to the module directly. The module's whole parameter set was already
described in the code, in the 147-entry table behind `xbee_at_table.h`, along with each command's
type, width, documented range, factory default and access flags. Everything needed to read and
write any parameter generically was present; what was missing was a way for an operator to ask.

## Changes

### New files

| File | Layer | What it does |
| --- | --- | --- |
| `src/utils/inc/line_edit.h`, `src/utils/src/line_edit.c` | utils | Line discipline: printable input, backspace and DEL, CR / LF / CRLF, Ctrl-C, `?` as an immediate help request, masked echo for a password, and escape sequences swallowed so an arrow key cannot drop `[A` into the command line. Echo leaves through a callback, so the module depends on nothing but the C standard library |
| `src/services/inc/cli_parser.h`, `src/services/src/cli_parser.c` | services | Tokenizes a line and walks a `cli_node_t` tree, producing either a matched command with its arguments, a diagnostic, or the `?` listing for the current position. Pure: no XBee, no iostream, no modes beyond an opaque visibility bitmask |
| `src/drivers/inc/console_uart.h`, `src/drivers/src/console_uart.c` | drivers | The console's only door to the serial port. Non-blocking reads from the VCOM stream, writes through stdio with an explicit flush |
| `src/app/inc/cli.h`, `src/app/src/cli.c` | app | The command tree, the mode state machine, the password prompt, and the request states |
| `src/app/src/cli_show.h`, `src/app/src/cli_show.c` | app | The resumable `show xbee all` walk and `show xbee info` |
| `src/app/src/cli_value.h`, `src/app/src/cli_value.c` | app | Turning typed text into an AT parameter's wire form, and building a command's factory default |
| `src/app/inc/cli_config.h` | app | Build-time settings: password, hostname, line length, timeouts |
| `test/test_line_edit.c`, `test/test_cli_parser.c`, `test/test_cli_value.c` | test | 600 checks across the three pure modules |

### Changed files

- `app.c`: added `XBEE_APP_CLI` as application 4, with its `cli_init()` and `cli_process()` arms,
  and made it the default in the `#ifndef XBEE_APP` fallback.
- `cmake_gcc/CMakeLists.txt`: registered the six new sources in the `# Add additional sources here`
  section, and set `XBEE_APP=XBEE_APP_CLI` in both `target_compile_definitions` blocks. No include
  path changes were needed: all four layers were already listed, and `cli.h` is in `src/app/inc`,
  which the `slc` block already exposes to `app.c`.
- `test/CMakeLists.txt`: registered the three new test binaries.
- `src/utils/src/line_edit.c`: emits a bare LF rather than CR LF. See below.

### Decisions worth recording

**stdout is line buffered on this build, so a prompt needs an explicit flush.** `_isatty()` in the
SDK's `sl_iostream_retarget_stdio.c` returns 1 unconditionally, and `sl_iostream_stdlib_config.c`,
the file that calls `setvbuf(stdout, NULL, _IONBF, 0)`, is not in `cmake_gcc/xbee_provision.cmake`.
newlib therefore gives stdout `_IOLBF`. Every existing `APP_LOG_*` line ends in a newline and so
flushes itself, which is why this has never shown up; a prompt such as `xbee> ` does not, and would
sit in the buffer unseen. Every write in `console_uart.c` ends in an explicit `fflush(stdout)`, and
that is the reason the file exists at all rather than the console calling `printf()` directly.

**Output goes through stdio, input goes straight to the stream.** Writing directly to the stream
would race with the logger: `app_log_write()` is `printf()`, so its lines sit in the stdio buffer
while a direct write would overtake them, and the two would interleave out of order whenever
logging was switched on. Input has no such constraint and takes the shorter path.

**`sl_iostream_read()` was verified to be non-blocking here before anything was built on it.**
`uart_read()` in `sl_iostream_uart.c` waits on an event flag only under
`SL_CATALOG_KERNEL_PRESENT`; this is a bare-metal `sl_main` build with no kernel, so it drains the
receive ring and reports `SL_STATUS_EMPTY` when it is empty.

**`line_edit` emits LF, not CR LF.** The VCOM instance is configured with
`SL_IOSTREAM_EUSART_VCOM_CONVERT_BY_DEFAULT_LF_TO_CRLF`, so the transport owns the line ending;
emitting the CR as well would send it twice. `app_log` already writes its lines this way, so the
whole project now has one convention.

**Value parsing was split out into its own module so it could be tested.** It is the part where a
mistake writes a wrong value into a module's flash rather than merely printing something odd.
`test_cli_value.c` runs against real table entries, and one case walks every provisionable command
in the table, checking that the default `cli_value_default()` builds reads back as that command's
default and passes `xbee_at_table_validate_set()`. That is what makes `no xbee at <NAME>` safe to
offer across the whole table instead of a hand-picked list.

**Parsing and range checking are kept apart.** `cli_value_parse()` decides only how the text is
read and how wide the result is; the documented bounds stay with the table, in
`xbee_at_table_validate_set()`. The console calls both before sending anything, so an out-of-range
value is refused locally with its range quoted rather than producing a bare `ERROR` from the module.

**Leading zero bytes do not count against a parameter's width.** Writing `000C` to a one-byte
parameter names the same value as `0C`, and refusing it would be pedantry. Only significant bytes
can overflow.

**Ctrl-C during `show xbee all` finishes the read in flight before stopping.** The transport holds
a pointer to the request object until the reply or the timeout arrives, so clearing the walk's
state on the spot and reusing that object would let a late completion write into a request the
console had moved on from. The walk therefore stops before starting the *next* read, at most one
command timeout later. For the same reason Ctrl-C does nothing during a single read, write or
`write memory`, which are bounded by a one to five second timeout anyway.

**The walk is a second implementation, not a reuse of `xbee_dump.c`.** The two differ where it
matters: this one yields to input between parameters, stops part way through on request, and
returns to a prompt, where the dump's runs once at boot and halts. What is shared is everything
that decides *what* to print: `xbee_dump_is_dumpable()`, `xbee_dump_category_name()` and
`xbee_dump_format_value()`. `xbee_dump.c` was left untouched because it has been verified on
hardware. Unifying the two behind one resumable walker is recorded as a follow-up.

**Logging starts off.** `app_log` writes to the same VCOM stream, so a log line would land in the
middle of whatever the operator is typing. `cli_init()` calls
`app_log_set_level(APP_LOG_LEVEL_NONE)`, and `logging level <lvl>` turns it back on, warning that
the output will interleave. `logging level verbose` reports that the level was compiled out unless
the build defines `APP_LOG_LEVEL_MIN=0`, rather than failing silently.

**Persistence follows the Cisco model, which is also the module's own.** A set is the running
configuration and applies at once; `write memory` commits it with `ATWR` (manual lines 3101 and
3111). Committing on every set was rejected: it would spend module flash endurance on every
keystroke-level edit. Leaving configuration mode with uncommitted changes prints a reminder.

**The password is not a security boundary, and the code says so.** It is plain text in flash and
travels in the clear over the wire. `handle_password()` uses `strcmp` rather than a constant-time
comparison, with a comment explaining that timing is not the weak part. Persisting a changed
password would need the `nvm3` component, which this project does not install.

## Simplicity Studio changes (made by the user)

**None.** VCOM receive was already routed on PD07 and configured
(`SL_IOSTREAM_EUSART_VCOM_RX_BUFFER_SIZE` 32,
`SL_IOSTREAM_EUSART_VCOM_RESTRICT_ENERGY_MODE_TO_ALLOW_RECEPTION` 1), and the `iostream_eusart`
instance is bidirectional as installed. No component was added and no configuration was changed.

One recommendation was made and not acted on: raising
`SL_IOSTREAM_EUSART_VCOM_RX_BUFFER_SIZE` from 32 to 128 in the `iostream_eusart` (`VCOM`) component
editor, because VCOM has no flow control and a pasted line longer than 32 characters arriving
faster than one super-loop pass will lose characters. The console mitigates this by draining the
receive buffer on every pass in every state, including while it is busy, but that is mitigation
rather than a fix.

## Verification

Build, host tests, and **run on the board** against a live XBee (0013A200427A96FB, firmware 0x2012,
hardware 0x4153, API 1 mode).

- All four applications build with no warnings under the project's flags:

  | Build | `.text` | `.bss` |
  | --- | --- | --- |
  | `XBEE_APP_BRINGUP` | 50 516 | 261 668 |
  | `XBEE_APP_DUMP` | 53 188 | 261 668 |
  | `XBEE_APP_PROVISION` | 55 700 | 261 668 |
  | `XBEE_APP_CLI` | 60 380 | 261 660 |

  The three existing variants are byte for byte identical to the figures recorded in
  `docs/plan/2026-09-20-xbee-at-dump-app.md`, which is the evidence that this change did not
  perturb them. The console adds 4680 bytes of flash over the provisioning build, 0.46 % of the
  part, and its `.bss` comes out 8 bytes lower because its statics displace the provisioning
  application's rather than adding to them.

- Host tests: 12 binaries, all passing.

  ```sh
  cmake -S test -B test/build && cmake --build test/build
  ctest --test-dir test/build --output-on-failure
  ```

  The three new ones contribute 600 checks: `test_line_edit` 242, `test_cli_parser` 267,
  `test_cli_value` 91. They cover backspace at column zero, CRLF coalescing with a reset in
  between, Ctrl-C, line overflow and its swallowed tail, masked and silent echo, `?` being literal
  inside a password, four shapes of escape sequence; tokenizing, keyword and argument matching,
  mode filtering, the unknown, incomplete and too-many diagnostics, help at every depth of the tree
  and the `<cr>` marker; and value parsing for every type, padding, leading zeros, over-wide values,
  the parse-then-validate join, and a sweep of every provisionable command's default.

- Static review: the console starts no request of its own while the walk owns one, only one
  `xbee_at_req_t` is ever in flight per owner, and every `sl_status_t` returned by the facade is
  checked.

- On target, flashed with J-Link and driven over VCOM at 115200 8N1. All eight checks in the plan
  pass; the plan records each one's result. The highlights:

  - The prompt appears after bring-up, which is the direct test of the line-buffering finding.
  - `?` narrows correctly at every depth, reports `<cr>` for a complete command, and leaves the
    half-typed line in place to carry on from.
  - `CH = 12 (0x0C)`, `ID = 13106 (0x3332)`, `NI = "WTX"`: integers and text both render correctly.
  - The full walk printed 124 parameters and 2 unreadable in 5681 ms, against the 4.5 s estimated
    for API mode. Ctrl-C stopped it at 48 parameters in 1541 ms and returned to the prompt.
  - Three wrong passwords gave `% Bad passwords`; `admin` reached `xbee# `.
  - `xbee at CH 0F` read back as `15 (0x0F)`; `write memory` then a reset, which power cycles the
    module, and it still read `15 (0x0F)`.
  - Out-of-range, non-hexadecimal and read-only writes were all refused locally, before anything
    reached the module.
  - Backspace and a 145-character line both behaved.

  The module was left exactly as found: `no xbee at CH` and `write memory` put CH back to
  `12 (0x0C)`, confirmed after a further reset, with ID and NI unchanged.

### Two defects found on hardware

**`show` was not reachable in configuration mode.** The plan put the `show` subtree at EXEC level
only, which is what IOS proper does: inside global configuration you write `do show ...`. On the
board this meant setting a parameter and then being unable to read it back without leaving the
mode, which is exactly the loop this console exists to serve. The whole `show` subtree is now
visible in every mode. This is a deliberate and documented departure from strict IOS behaviour, and
the reason is in a comment beside the mode masks in `cli.c`.

**The out-of-range message did not pad the bounds.** `CH` reported `0xB to 0x1A`, where the value
itself prints as `0x0C`. The bounds are now padded to the parameter's declared width.

Both were fixed, rebuilt, reflashed and re-checked on the board.

## Known limitations and follow-ups

- The console's walk and `xbee_dump.c`'s walk are two implementations of nearly the same loop.
  Worth unifying behind one resumable walker.
- No `logging synchronous`: with logging switched on, output still interleaves with a half-typed
  command. Redrawing the prompt after each log line would need an `app_log` hook.
- No command abbreviation, tab completion or command history; keywords must be typed in full. The
  parser was built so that abbreviation is a change to `match_child()` alone.
- A pasted line longer than the 32-byte VCOM receive buffer may lose characters. See the Studio
  recommendation above.
- The enable password cannot be changed at runtime, and does not survive a reboot, because it is a
  compile-time `#define`. Persisting one needs `nvm3`.
- `show xbee all` skips the multi-response commands (`ND`, `VL`, `AS`, `ED`) and the subcommand
  interpreters, because the facade discards multi-line replies. This is the same limitation the
  parameter dump has, and fixing it needs a services layer change.
