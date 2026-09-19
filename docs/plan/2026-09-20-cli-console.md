# Cisco-style CLI console over VCOM

Status: Done
Date: 2026-09-20
Related history: docs/history/2026-09-20-cli-console.md

Implemented, builds without warnings in all four application variants, the host tests pass, and
**every on-target check under "Verification" was run on the board** against a live XBee
(0013A200427A96FB, firmware 0x2012, API 1 mode). Two defects were found on hardware and fixed; both
are recorded below and in the history entry.

Departures from the plan as written, all recorded in the history entry:

- **`show` is available in configuration mode.** The plan put the `show` subtree at EXEC level
  only, which is what IOS proper does: it makes you write `do show ...` inside global
  configuration. On the board that turned out to be the wrong trade. Writing a parameter and being
  unable to read it back without leaving the mode is precisely the loop this console exists to
  serve, so the whole `show` subtree is visible in every mode instead. Found by check 6.
- The out-of-range message now pads the bounds to the parameter's declared width, so they read
  `0x0B to 0x1A` rather than `0xB to 0x1A`, matching how the value itself is printed.
- There are six new modules, not five. Value parsing was pulled out of `cli.c` into
  `src/app/src/cli_value.{h,c}` so that it could be host tested: it is the part where a mistake
  writes a wrong value into a module's flash, rather than merely printing something odd.
- `line_edit` emits a bare LF, not CR LF. The VCOM instance is configured with
  `SL_IOSTREAM_EUSART_VCOM_CONVERT_BY_DEFAULT_LF_TO_CRLF`, so the transport owns the line ending
  and emitting CR here as well would send it twice. `app_log` already writes its lines this way.
- Ctrl-C during `show xbee all` does not stop the walk on the spot. It finishes the read already in
  flight, at most one command timeout, then stops. The transport holds a pointer to the request
  object until the reply or the timeout arrives, so reusing it sooner would let a late completion
  write into a request the console had moved on from.
- Ctrl-C does nothing during a single read, write or `write memory`, for the same reason. Those are
  bounded by a one to five second timeout, so there is little to gain and a live pointer to respect.
- The flash estimate was too high. The console costs 4680 bytes, not the 5 to 7 kB estimated below.
  Measured figures are in "Resource impact".

## Goal

Add a fourth application, `XBEE_APP_CLI`, that turns the VCOM debug link into an interactive
console with Cisco IOS command syntax and mode structure, so that an XBee module can be inspected
and reconfigured on a live board with no rebuild, no reflash and no XCTU.

Measurable outcome, over a 115200 8N1 terminal on VCOM:

- A prompt `xbee> ` after bring-up, and three modes: user EXEC `xbee> `, privileged EXEC `xbee# `
  reached by `enable` with a password, and global configuration `xbee(config)# ` reached by
  `configure terminal`.
- `show xbee at CH` prints one decoded parameter, `show xbee all` prints every readable parameter
  grouped by the manual's categories. Measured on the board: 124 parameters in 5.7 s over an API
  mode link, against the 4.5 s extrapolated from the parameter dump's figures.
- `xbee at CH 0F` in configuration mode writes one parameter, with the value range checked locally
  against the command table before anything reaches the wire.
- `?` gives IOS-style context help at any point on the line, generated from the same command tree
  that dispatch uses.

Non-goals: the three existing applications are unchanged and still buildable; command abbreviation,
tab completion and command history are out of scope; the console is not a security boundary.

## Context

The firmware today is three fixed, boot-time applications selected by the `XBEE_APP` macro in
`app.c`. `XBEE_APP_PROVISION` writes a configuration baked into `xbee_provision_config.h`,
`XBEE_APP_BRINGUP` reports what the module is, and `XBEE_APP_DUMP` prints every readable AT
parameter once and stops. Changing one parameter on a board means editing a header, rebuilding and
reflashing, or attaching XCTU to the module directly.

Almost everything an interactive console needs already exists and is reused rather than rewritten.
This plan is mostly about the missing input path and the command grammar on top of it.

| Reused | From | What it gives the console |
| --- | --- | --- |
| `xbee_at_table_find`, `_at`, `_count`, `_name`, `xbee_at_id_to_str` | `src/services/inc/xbee_at_table.h` | Lookup of any of the 147 commands by name, and the name back for output |
| `xbee_at_table_validate_get`, `_validate_set` | same | Local access and range checking, so a bad value never reaches the module |
| `xbee_at_get`, `_set`, `_exec`, `xbee_at_req_complete`, `xbee_get_info`, `xbee_hw_reset` | `src/services/inc/xbee.h` | Non-blocking request path that opens and keeps a Command mode session by itself |
| `xbee_dump_format_value`, `xbee_dump_is_dumpable`, `xbee_dump_category_name` | `src/app/inc/xbee_dump_format.h` | Type-decoded rendering, the skip rule and category headings. All three are pure and already host-tested |
| `byte_util_hex_decode`, `_hex_to_u32`, `_hex_to_u64`, `byte_util_write_be16/32/64` | `src/utils/inc/byte_util.h` | Parsing a typed value into its big-endian wire form |

## Background and references

- `docs/manuals/xbee_90002273_ref_manual.md`, `Parameter format` (line 3095): a Command mode
  parameter is hexadecimal, with or without a `0x` prefix. This is why the console accepts
  hexadecimal for every integer type rather than inventing a decimal convention.
- Same file, `Apply command changes` (line 3101) and `Make command changes permanent` (line 3111):
  a set applies at once or on leaving Command mode, and `WR` is what commits it to the module's
  flash. This is the basis for mapping `write memory` onto `ATWR` rather than committing on every
  set.
- Same file, `AT commands` (line 4812 onwards): the command set the 147-entry table already
  encodes, including each command's range and default.
- `docs/plan/2026-09-20-xbee-at-dump-app.md`, "Timing budget": the measured cost of walking the
  whole table, 3.5 s in Command mode and 4.5 s in API mode, which `show xbee all` inherits.
- Simplicity SDK 2025.6.2, `platform/service/iostream/src/sl_iostream_uart.c`, `uart_read()`, and
  `platform/service/iostream/src/sl_iostream_retarget_stdio.c`, `_isatty()`. See "Verified platform
  constraints" below.

### Verified platform constraints

Checked against the SDK sources and the generated configuration, not assumed. Two of them change
the design.

1. **VCOM receive already works.** `config/pin_config.h` routes EUSART2 RX on PD07 and
   `config/sl_iostream_eusart_VCOM_config.h` sets `SL_IOSTREAM_EUSART_VCOM_RX_BUFFER_SIZE` to 32
   with `SL_IOSTREAM_EUSART_VCOM_RESTRICT_ENERGY_MODE_TO_ALLOW_RECEPTION` at 1. The instance is
   bidirectional as installed, so the console needs no Simplicity Studio change to read input.
2. **`sl_iostream_read()` is non-blocking in this build.** `uart_read()` waits on an event flag
   only under `SL_CATALOG_KERNEL_PRESENT`. This is a bare-metal `sl_main` build with no kernel, so
   the function drains the RX ring and returns `SL_STATUS_EMPTY` when there is nothing there. That
   satisfies the super-loop contract: no blocking read, no busy-wait.
3. **stdout is line buffered, so a prompt will not appear without an explicit flush.** `_isatty()`
   in `sl_iostream_retarget_stdio.c` returns 1 unconditionally, and `sl_iostream_stdlib_config.c`,
   the file that would call `setvbuf(stdout, NULL, _IONBF, 0)`, is not in
   `cmake_gcc/xbee_provision.cmake`. newlib therefore selects `_IOLBF` for stdout. Every existing
   `APP_LOG_*` line ends in `\n`, so this has never shown up; a prompt such as `xbee> ` does not,
   and would sit in the buffer invisibly. The console must `fflush(stdout)` after writing a prompt
   and after echoing input. Recorded here because it is the most likely cause of a "nothing appears
   on the terminal" bring-up failure.
4. **`nvm3` is not installed.** `xbee_provision.slcp` lists only `clock_manager`, `device_init`,
   `sl_main`, `uartdrv_eusart` (`XBEE`), `iostream_eusart` (`VCOM`) and `iostream_retarget_stdio`.
   The enable password is therefore a compile-time `#define` with no runtime persistence, which is
   the decision taken for this change and needs no Studio change.
5. **Log output shares the VCOM link with the console.** `app_log_write()` is `printf()` onto the
   same stream, so any `APP_LOG_*` line emitted by `xbee.c` or `xbee_cmd_mode.c` would land in the
   middle of a half-typed command. Handled under "Logging".

## Design

### Mode structure

Three levels, as on IOS. The password guards `enable`, so configuration mode is password protected
by construction rather than by a second check.

| Mode | Prompt | Entered by | Left by |
| --- | --- | --- | --- |
| User EXEC | `xbee> ` | boot | — |
| Privileged EXEC | `xbee# ` | `enable` plus password | `disable`, `exit` |
| Global configuration | `xbee(config)# ` | `configure terminal` | `exit`, `end` |

### Files

All new, under `src/`, following the layering rule in `CLAUDE.md`: `app` -> `services` ->
`drivers` -> SDK, with `utils` usable by any layer and depending on no project layer.

| File | Layer | Responsibility |
| --- | --- | --- |
| `src/drivers/inc/console_uart.h`, `src/drivers/src/console_uart.c` | drivers | Wrapper over the VCOM iostream: `console_uart_init()`, `console_uart_read()` returning `SL_STATUS_EMPTY` when idle, `console_uart_write()`, `console_uart_flush()`. The only file that names `sl_iostream_VCOM_handle`, and the owner of the `fflush(stdout)` from constraint 3 |
| `src/utils/inc/line_edit.h`, `src/utils/src/line_edit.c` | utils | Hardware-independent line discipline. Echo is emitted through a caller-supplied callback, so the module depends on nothing but the C standard library and is host-testable |
| `src/services/inc/cli_parser.h`, `src/services/src/cli_parser.c` | services | Table-driven tokenizing and command-tree matching, and the `?` help listing. Pure: no XBee, no iostream, no logging. Host-testable |
| `src/app/inc/cli.h`, `src/app/src/cli.c` | app | `cli_init()` and `cli_process()`, the mode state machine, the command tree with its handlers, the password prompt, and the request states |
| `src/app/src/cli_show.h`, `src/app/src/cli_show.c` | app | The `show` handlers, including the resumable `show xbee all` walk. `cli_show.h` is a private header living beside its `.c`, per the layering rule |
| `src/app/inc/cli_config.h` | app | Build-time settings |

### Interfaces

`line_edit` reports one event per byte fed to it:

```c
typedef enum {
  LINE_EDIT_NONE,      ///< Byte consumed, nothing to do yet.
  LINE_EDIT_READY,     ///< A complete line is available.
  LINE_EDIT_HELP,      ///< '?' was typed; show help for the line so far.
  LINE_EDIT_ABORT,     ///< Ctrl-C. The line was discarded.
  LINE_EDIT_OVERFLOW,  ///< The line exceeded its buffer and was discarded.
} line_edit_event_t;

line_edit_event_t line_edit_feed(line_edit_t *le, char c);
```

It handles printable ASCII, backspace and DEL, CR and LF with CRLF coalescing, and a no-echo mode
for the password. `?` is intercepted and never enters the buffer, so the line survives a help
request and is redisplayed after it, which is IOS behaviour.

`cli_config.h`, every setting `#ifndef` guarded so a build can override it with a compiler define:

```c
#ifndef CLI_ENABLE_PASSWORD
#define CLI_ENABLE_PASSWORD      "admin"
#endif
#ifndef CLI_HOSTNAME
#define CLI_HOSTNAME             "xbee"
#endif
#ifndef CLI_LINE_MAX
#define CLI_LINE_MAX             128U    ///< Longest command line, terminator included.
#endif
#ifndef CLI_MAX_TOKENS
#define CLI_MAX_TOKENS           8U
#endif
#ifndef CLI_PASSWORD_ATTEMPTS
#define CLI_PASSWORD_ATTEMPTS    3U      ///< Then back to user EXEC.
#endif
#ifndef CLI_PASSWORD_TIMEOUT_MS
#define CLI_PASSWORD_TIMEOUT_MS  30000U
#endif
#ifndef CLI_REQUEST_TIMEOUT_MS
#define CLI_REQUEST_TIMEOUT_MS   5000U   ///< Stall bound on dispatching one request.
#endif
```

### Command set

| Mode | Command | Action |
| --- | --- | --- |
| all modes | `show xbee at <NAME>` | `xbee_at_get()`, rendered with `xbee_dump_format_value()` |
| all modes | `show xbee all` | Resumable walk of the command table |
| all modes | `show xbee info` | `xbee_get_info()`: SH and SL, VR, HV, AP, detected mode |
| all modes | `show version` | Firmware build identity and module identity |
| user | `enable` | Password prompt, no echo, up to `CLI_PASSWORD_ATTEMPTS` |
| all modes | `exit` | User EXEC: logout banner. Privileged: back to user EXEC |
| priv | `disable` | Back to user EXEC |
| priv | `configure terminal` | Enter global configuration |
| priv | `write memory` | `xbee_at_exec(XBEE_AT_WR)` with a 5 s timeout |
| priv | `reload` | `xbee_hw_reset()`, then bring-up runs again |
| priv | `logging level {verbose\|info\|warning\|error\|none}` | `app_log_set_level()` |
| config | `xbee at <NAME> <VALUE>` | Parse and validate against the table, then `xbee_at_set()` |
| config | `no xbee at <NAME>` | Write that command's documented factory default from the table |
| config | `exit`, `end` | Back to privileged EXEC |

Persistence follows the Cisco model, which maps onto the XBee's own documented behaviour: a set is
the running configuration and applies at once, and `write memory` is what commits it with `ATWR`
(manual lines 3101 and 3111). The console prints a reminder when configuration mode is left with
uncommitted changes. Committing on every set was rejected: it would spend module flash endurance on
every keystroke-level edit for no benefit.

### Value parsing

Driven entirely by the table entry, so there is no per-parameter code:

- `U8`, `U16`, `U32`, `U64`, `BITMAP`: hexadecimal, with or without a `0x` prefix, per the manual's
  `Parameter format`. Decoded with `byte_util_hex_to_u32()` or `_u64()`, then written big-endian to
  `entry->max_len` bytes with `byte_util_write_be16/32/64()`.
- `STRING`: taken as typed, bounded by `entry->max_len`.
- `BYTES`: an even-length hexadecimal string through `byte_util_hex_decode()`.
- `EXEC` and `SUBCOMMAND`: refused, because they carry no value to write.

`xbee_at_table_validate_set()` then checks width and range before the request is submitted, so an
out-of-range value is refused locally with the documented bounds quoted back, instead of producing
a bare `ERROR` from the module.

### Help

Help is generated by `cli_parser` from the same `cli_node_t` tree that dispatch walks, so the two
cannot drift apart.

```
xbee> ?
  enable     Turn on privileged commands
  exit       Exit from the EXEC
  show       Show running system information

xbee> show xbee ?
  all   All readable AT parameters
  at    One AT parameter by name
  info  Module identity and detected mode

xbee> show xbee at ?
  <NAME>  Two-character AT command, for example CH, ID, NI

xbee> show xbee all ?
  <cr>
```

`<cr>` is printed when the node reached is itself a complete command. Keywords must be typed in
full; `?` after a partial word lists the keywords that begin with it, which is display only.

### States

`cli_process()` is called from `app_process_action()` on every super-loop iteration and returns
promptly. It calls `xbee_process()` first, then services input, then advances whatever is in flight.

```
CLI_STATE_BRINGUP   waiting for the facade to find the module; input is drained, not parsed
CLI_STATE_PROMPT    idle at a prompt, feeding bytes to line_edit
CLI_STATE_PASSWORD  as above, echo suppressed, CLI_PASSWORD_TIMEOUT_MS deadline
CLI_STATE_GET       one xbee_at_get in flight
CLI_STATE_SET       one xbee_at_set in flight
CLI_STATE_EXEC      one xbee_at_exec in flight (write memory, reload)
CLI_STATE_WALK      show xbee all: read, print, advance, repeat
```

Only one `xbee_at_req_t` is ever in flight, which is what the facade requires. Input is drained in
every state, including the busy ones, so Ctrl-C aborts; that matters most for `show xbee all`,
which runs for several seconds. Other input received while busy is discarded rather than queued, as
IOS does.

A dispatch that returns `SL_STATUS_NOT_READY` or `SL_STATUS_BUSY` is retried until
`CLI_REQUEST_TIMEOUT_MS` rather than abandoned, the same pattern `start_next_read()` uses in
`src/app/src/xbee_dump.c`: in Transparent mode the transport may be re-entering Command mode
underneath.

### The `show xbee all` walk

`xbee_dump_is_dumpable()`, `xbee_dump_category_name()` and `xbee_dump_format_value()` are reused
unchanged from `src/app/src/xbee_dump_format.c`. The walk loop itself is written again in
`cli_show.c` rather than lifted out of `xbee_dump.c`, because the two differ in their exit
conditions: the console's walk must yield to input polling between parameters, abort on Ctrl-C and
return to a prompt, where the dump's halts. `xbee_dump.c` is left untouched, since it has been
verified on hardware. Unifying the two behind one resumable walker is a worthwhile follow-up and is
recorded as one rather than done here.

### Logging

The console sets `app_log_set_level(APP_LOG_LEVEL_NONE)` in `cli_init()` so that stack log lines
cannot corrupt a half-typed command, and `logging level <lvl>` turns them back on for debugging.
`APP_LOG_LEVEL_MIN` is `APP_LOG_LEVEL_INFO` at compile time, so `logging level verbose` returns
`SL_STATUS_INVALID_RANGE` unless the build defines `APP_LOG_LEVEL_MIN=0`; the handler reports that
rather than failing silently. IOS's `logging synchronous`, which redraws the prompt after each log
line, would need an `app_log` hook and is left as a follow-up.

### Failure handling

| Condition | Response |
| --- | --- |
| Unknown or incomplete command | `% Unknown command` / `% Incomplete command`, prompt returns |
| Command not in the AT table | `% Unknown AT command <NAME>`, nothing sent |
| Read of a write-only or execute-only command | Refused locally by `xbee_at_table_validate_get()` |
| Value out of range or wrong width | Refused locally by `xbee_at_table_validate_set()`, documented bounds printed |
| Module answers `ERROR` (`SL_STATUS_FAIL`) or times out | One line carrying the status, prompt returns; the console never halts |
| Bring-up fails | Banner with `xbee_get_result()`, then the prompt still comes up so `reload` can be tried |
| Line longer than `CLI_LINE_MAX` | `LINE_EDIT_OVERFLOW`, line discarded, `% Line too long` |

### Build registration

`cmake_gcc/CMakeLists.txt`, inside the marked sections only: the six new `.c` files under
`# Add additional sources here`. All four layer include paths are already present, and `cli.h` lives
in `src/app/inc`, which is already in the `target_include_directories(slc PUBLIC ...)` block, so no
include path changes are needed.

`app.c` gains `#define XBEE_APP_CLI 4` and its `cli_init()` / `cli_process()` arms, and
`XBEE_APP=XBEE_APP_CLI` is set in **both** `target_compile_definitions` blocks, per the warning
comment already in that file.

## Simplicity Studio changes required (PRIME RULE)

**None.** VCOM RX is routed and configured, the iostream instance is bidirectional as installed,
and the password is compile-time.

Two recommendations, neither acted on without an explicit instruction:

| Tool | Component / instance | Setting | Current value | Suggested value |
| --- | --- | --- | --- | --- |
| Software Components | `iostream_eusart`, instance `VCOM` | `SL_IOSTREAM_EUSART_VCOM_RX_BUFFER_SIZE` | 32 | 128 |

32 bytes is ample for typing, but VCOM has no flow control, so a pasted line longer than 32
characters arriving faster than one super-loop iteration will drop characters. The design mitigates
this by draining the RX ring on every `cli_process()` call in every state, but that is mitigation,
not a fix.

Separately, `APP_LOG_LEVEL_MIN=0` in `cmake_gcc/CMakeLists.txt` would make `logging level verbose`
usable. That is an application build file rather than a Studio-owned one.

## Resource impact

- **Flash**, measured with `arm-none-eabi-size` on the built images rather than estimated:

  | Build | `.text` | Note |
  | --- | --- | --- |
  | `XBEE_APP_BRINGUP` | 50 516 | Smallest variant |
  | `XBEE_APP_DUMP` | 53 188 | |
  | `XBEE_APP_PROVISION` | 55 700 | The previous default |
  | `XBEE_APP_CLI` | 60 380 | **What ships by default** |

  The console build is **4680 bytes** above the provisioning build, which is 0.46 % of the 1024 kB
  part. That is the net figure: the console's own code, command tree and help strings, less the
  108-entry provisioning table and `xbee_provision.c` that `--gc-sections` drops from this build.
  The three existing variants came out byte for byte identical to the figures recorded in
  `docs/plan/2026-09-20-xbee-at-dump-app.md`, which is the evidence that nothing existing moved.
- **RAM (.bss)**: 261 660 bytes, which is 8 bytes **below** the other three variants rather than
  above them. The console's statics, one `xbee_at_req_t`, two 128-byte line buffers, the editor and
  the mode and walk state, displace the provisioning application's own rather than adding to them,
  and the figure is dominated by the SDK heap and stack regions either way.
- **Stack**: one `XBEE_DUMP_VALUE_TEXT_CAP` buffer of 160 bytes plus a newlib-nano `vfprintf` frame
  of 150 to 200 bytes, so about 400 bytes at peak. `xbee_dump.c` already reaches a comparable
  depth, so this is not a new worst case.
- **Peripherals**: unchanged. EUSART0 for the XBee at 9600 through UARTDRV instance `XBEE`,
  EUSART2 for VCOM at 115200 through iostream, the sleeptimer, and `XBEE_EN_GPIO` on PA00. No new
  peripheral, no new interrupt.
- **Energy**: unchanged, and worth stating plainly. The free-running super loop and both EUSARTs
  already hold the device in EM0, and
  `SL_IOSTREAM_EUSART_VCOM_RESTRICT_ENERGY_MODE_TO_ALLOW_RECEPTION` was already 1, so VCOM receive
  was already keeping the part out of EM2. An interactive console is by nature an EM0 application:
  it must be able to react to a keystroke at any moment. This build is not a candidate for low
  power work.

## Risks and open questions

| Risk | Handling |
| --- | --- |
| Prompt invisible because stdout is line buffered | Explicit `fflush(stdout)` in `console_uart_write()`. First thing to check on bring-up, and the reason constraint 3 is written down |
| Pasted input longer than the 32-byte VCOM RX buffer | Drain on every iteration in every state; `LINE_EDIT_OVERFLOW` on an over-long line. Studio fix recommended above |
| A stack log line corrupts a typed command | Logging is off by default in this build |
| The Command mode session lapses during a walk | The transport re-enters on the next dispatch; `CLI_REQUEST_TIMEOUT_MS` is longer than the guard time plus margins plus the `OK` |
| A command this module variant does not implement, for example SPI `P5` to `P9` on a through-hole part | One error line carrying the status, the walk continues. Same tolerance `xbee_dump.c` already has |
| The password is plaintext in flash and is sent in the clear over a wire | Accepted and stated. This is a debug console on a wired serial port, not a security boundary, and the readme must say so plainly |
| The terminal emulator sends CRLF, LF or CR | `line_edit` coalesces CRLF and accepts either alone |

Open questions:

- Whether `CLI_HOSTNAME` should stay `xbee`.
- Whether to raise the VCOM RX buffer in Studio, as recommended above.
- Whether a later change should unify the console's walk with the dump's behind one resumable
  walker, and expose `logging synchronous`.

## Verification

### Host tests

New `test/test_line_edit.c` and `test/test_cli_parser.c`, registered in `test/CMakeLists.txt`
beside the existing nine binaries.

```sh
cmake -S test -B test/build && cmake --build test/build
ctest --test-dir test/build --output-on-failure
```

Coverage: backspace at column zero, CRLF coalescing, Ctrl-C, line overflow, no-echo mode; and
tokenizing with repeated and trailing spaces, the unknown, incomplete and ambiguous diagnostics,
help listings at every depth of the tree, and argument capture for `xbee at CH 0F`.

### Build checks

No new warnings, and all four applications still compile.

```sh
cd cmake_gcc && cmake --workflow --preset project
arm-none-eabi-size build/base/xbee_provision.out
```

### On target

Flashed with the J-Link tooling already set up for this project, over a 115200 8N1 terminal on
VCOM.

All eight were run on 2026-09-20 against a live module. Results:

1. **Pass.** `xbee> ` appeared after bring-up. This is the direct test of constraint 3: a prompt
   with no trailing newline reached the terminal, so the explicit flush works.
2. **Pass.** `?` listed the three user EXEC commands; `show ?`, `show xbee ?` and `show xbee at ?`
   each narrowed correctly; `show xbee all ?` reported `<cr>`; `show xbee a?` narrowed to `all`
   and `at`.
3. **Pass.** `CH (Operating Channel) = 12 (0x0C)`. Also `ID = 13106 (0x3332)` and
   `NI (Node Identifier) = "WTX"`, so integer and text parameters both render correctly.
4. **Pass.** The full walk printed 174 lines under their category headings: 124 read, 2 not
   readable, in 5681 ms. Ctrl-C part way through stopped it at 48 read in 1541 ms and returned to
   the prompt.
5. **Pass.** `configure terminal` at `xbee> ` was refused with a caret under `configure`. Three
   wrong passwords, echoed as asterisks, gave `% Bad passwords` and dropped back to `xbee> `.
   `admin` reached `xbee# `.
6. **Pass, after a fix.** `xbee at CH 0F` was accepted and read back as `15 (0x0F)`.
   `xbee at CH FF` was refused with `% Value out of range for CH: 0x0B to 0x1A`, `xbee at CH zz`
   with `% Value is not valid for CH, expected 8-bit number`, and `xbee at SH 1` with
   `% SH cannot be written`. The read-back is what exposed the `show`-in-configuration-mode defect
   above; it passes with the fix in place.
7. **Pass.** `write memory` reported `[OK]`, and after a reset, which power cycles the module
   because `CLI_POWER_CYCLE` is 1, `show xbee at CH` still read `15 (0x0F)`.
8. **Pass.** Backspace erased two characters mid-line and the retyped command ran correctly. A
   145-character line gave `% Line is longer than 127 characters` and a fresh prompt, and the
   swallowed tail did not run as a command.

The module was left exactly as it was found: `no xbee at CH` followed by `write memory` restored
CH to `12 (0x0C)`, confirmed after a further reset, with ID and NI unchanged.
