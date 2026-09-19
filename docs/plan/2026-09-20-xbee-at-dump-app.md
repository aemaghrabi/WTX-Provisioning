# XBee AT parameter dump application

Status: Done, pending on-target verification
Date: 2026-09-20
Related history: docs/history/2026-09-20-xbee-at-dump-app.md

Implemented, builds without warnings in all three application variants, and the
host tests pass. **It has not been run on hardware.** The on-target checks under
"Verification" are outstanding.

Departures from the plan as written, all recorded in the history entry:

- The flash estimate was wrong. `XBEE_AT_TABLE_NAMES=1` costs 4008 bytes on the
  provisioning build, not the roughly 2.6 kB estimated here. Measured figures are
  in "Resource impact" below.
- The dump application reports four counts rather than three. Commands that carry
  no value at all and commands that are deliberately not asked for are counted
  separately, because collapsing them would make the seven skipped action
  commands look like part of the 21 that simply have nothing to report.
- `start_next_read()` does not chain from `handle_read_result()`. The walk
  returns to the super loop between parameters, so a stalled dispatch is retried
  through the same path as an ordinary step rather than through a second one.

## Goal

Give the project a build that prints, at boot and over VCOM alone, every readable AT parameter read
from the XBee module: decoded to its type, named, and grouped by the manual's command categories.
A board must be inspectable without attaching XCTU.

Measurable outcome: 126 parameter lines of the form `CH (Operating Channel) = 12 (0x0C)`, grouped
under 22 category headings, followed by a summary that states how many parameters were read, how
many the module would not report, and how many were deliberately not asked for.

Non-goals: the dump writes nothing to the module, and the existing provisioning application's logs
are unchanged.

## Context

The provisioning application prints a *provisioning transcript*, not a configuration: one identity
line, then only the parameters that differ from `src/app/inc/xbee_provision_config.h`. Every
parameter that already matches is deliberately dropped (`src/app/src/xbee_provision.c`, the
`// Matches.` branch of `handle_read_result()`). That is correct for provisioning and useless for
inspection.

This plan adds a third application variant beside `XBEE_APP_PROVISION` and `XBEE_APP_BRINGUP`,
selected by the same build-time `XBEE_APP` macro. Decisions taken with the user on 2026-09-20:

| Decision | Outcome |
| --- | --- |
| Which parameters | The whole AT command table, not the 108-entry provisioning table |
| Where it lives | A separate, dump-only application variant; provisioning's logs stay as they are |
| Value rendering | Decoded by the command's type, not raw hexadecimal |
| Line format | Mnemonic and human-readable name together: `CH (Operating Channel) = <value>` |
| Default build | Provisioning remains the default `XBEE_APP` |
| After the dump | Idle with the module still powered, so XCTU can be attached without a power cycle |

### What "all parameters" actually amounts to

`src/services/src/xbee_at_table.c` holds 147 entries, but they are not 147 readable parameters:

| Class | Count | Why |
| --- | --- | --- |
| `XBEE_AT_TYPE_EXEC` | 13 | `AS DA FP CN IS %P FR AC WR RE %F !C R1`, rejected by `xbee_at_table_validate_get()` |
| `XBEE_AT_FLAG_WRITE_ONLY` | 1 | `KY` |
| Queryable | 133 | Pass `xbee_at_table_validate_get()` |
| Action-like, skipped | 7 | `ND DN ED VL FS PY CB`, see Design |
| **Parameter lines printed** | **126** | Across 22 of the 24 categories |

`XBEE_AT_CAT_MEMORY` and `XBEE_AT_CAT_CUSTOM_DEFAULT` print no heading at all: every entry in them
is `EXEC`. The banner line states both 147 and 126 so that no one hunts for the missing 21.

## Background and references

All manual references are to `docs/manuals/xbee_90002273_ref_manual.md` (Digi XBee 3 802.15.4 RF
Module User Guide, 90002273 R).

| Topic | Lines | What it establishes |
| --- | --- | --- |
| `DN` (Discover Node) | 5053 to 5075 | Sent in Command mode, on success "the device exits Command mode to allow for immediate communication". Sent as an API frame, "if there is no response from a module within (NT * 100) milliseconds or you do not specify a parameter (by leaving it blank), the receiving device returns an ERROR message". `NT` defaults to 0x19, so that window is 2.5 s, well past either transport's one-second timeout |
| `ED` (Energy Detect) | 5643 to 5652 | "Starts an energy detect scan. The device loops through all the available channels and returns the maximal energy on each channel, a comma follows each value, and the list ends with a carriage return." An action that occupies the radio, not a stored parameter |
| `CB` (Commissioning Pushbutton) | 6344 to 6360 | "Use CB to simulate Commissioning Pushbutton presses in software." Parameter range `1,4`; `4` is "Restore defaults (equivalent to sending an RE (Restore Defaults))". Default `N/A`: there is no stored value to read back |
| `KY` write-only | 5449 to 5458 | "This command is write-only and cannot be read." Already excluded by the table's `WRITE_ONLY` flag |
| Command mode available in every operating mode | 3041 | `+++` works whether the module is in Transparent or API mode, which is why the transport can open a session by itself when one is needed |
| `CT` command mode timeout | 3062 to 3065 | Default 10 s of idle before the module leaves Command mode on its own |
| `WR` cost | 7089 to 7099 | 10 000 erase/write cycles. Named here only to record that the dump performs none |

MCU references: `docs/manuals/efm32pg28_reference_manual.md` is not needed for this change. No
peripheral, clock or energy-mode configuration is altered.

## Design

### Files

```
src/app/inc/xbee_dump.h           init / process / is_finished / get_result
src/app/src/xbee_dump.c           state machine and logging
src/app/inc/xbee_dump_config.h    #ifndef-guarded options
src/app/inc/xbee_dump_format.h    skip predicate, category names, value renderer
src/app/src/xbee_dump_format.c    hardware independent, host testable
```

The split mirrors `xbee_provision.c` plus `xbee_provision_table.c`. `xbee_dump_format.c` depends
only on `xbee_at_table.h`, `byte_util.h` and the C standard library, so the skip rule and the
formatter are proven on the host under `-Wall -Wextra -Werror`; only the sequencing needs hardware.

### Interfaces

```c
typedef enum {
  XBEE_DUMP_RESULT_RUNNING,
  XBEE_DUMP_RESULT_COMPLETE,      ///< The table was walked to the end.
  XBEE_DUMP_RESULT_FAIL_BRINGUP,  ///< The module never answered.
} xbee_dump_result_t;

sl_status_t        xbee_dump_init(void);
void               xbee_dump_process(void);
bool               xbee_dump_is_finished(void);
xbee_dump_result_t xbee_dump_get_result(void);
```

```c
#define XBEE_DUMP_VALUE_TEXT_CAP  160U

bool        xbee_dump_is_dumpable(const xbee_at_entry_t *entry);
const char *xbee_dump_category_name(uint8_t category);
const char *xbee_dump_format_value(const xbee_at_entry_t *entry,
                                   const uint8_t *value, uint16_t len,
                                   char *out, uint16_t cap);
```

### States

```
DUMP_IDLE --init--> DUMP_BRINGUP --ready--> DUMP_READ --exhausted--> DUMP_DONE
                          |
                    XBEE_STATE_FAILED -> DUMP_DONE (FAIL_BRINGUP)
```

- `xbee_dump_init()` uses the same `xbee_config_t` as `xbee_provision_init()`: `XBEE_MODE_AUTO`,
  `power_cycle` from the config header, configured defaults for settle and probe. It records
  `start_tick`, logs the banner, calls `xbee_init()` and enters `DUMP_BRINGUP`.
- `DUMP_BRINGUP`: on `XBEE_STATE_FAILED`, log `xbee_get_result()` and finish with `FAIL_BRINGUP`.
  No retry: `XBEE_APP_BRINGUP` already exists for a board that will not talk at all. On
  `xbee_is_ready()`, print the header block and start the first read.
- `DUMP_READ`: `start_next_read(from)` walks `xbee_at_table_at(i)`, skipping entries that fail
  `xbee_dump_is_dumpable()`, emits a category heading when `entry->category` changes, then issues
  `xbee_at_get(entry->id, &req)`. The table is already in manual order grouped by category, so
  change detection suffices and nothing needs sorting.
- `DUMP_DONE`: print the footer, then call only `xbee_process()` on each iteration so the 512-byte
  RX ring keeps draining, because an API-mode module still emits modem status frames. The module
  stays powered. No repeat.

### No Command mode session is opened

`xbee_at_get()` already works in every mode. If bring-up landed in Transparent mode,
`dispatch_request()` in `src/services/src/xbee.c` opens and maintains the session by itself. If it
landed in API mode, forcing a session would cost a full guard time of silence plus the `OK` wait
(`GT` 1000 ms + `XBEE_CMD_MODE_GUARD_MARGIN_MS` 50 + `XBEE_CMD_MODE_OK_MARGIN_MS` 500) and buy
nothing: the only `CMD_MODE_ONLY` command in the table is `FS`, which is skipped.

API framing is also strictly better for a dump. Per-command status bytes decode to distinct
`sl_status_t` values in `status_from_at()`, whereas Command mode collapses every failure to a bare
`ERROR` line and one generic result.

The file header must record that the dump may nevertheless put a Transparent-mode module into
Command mode through the transport, that this is not a write, and that the module leaves Command
mode on its own `CT` timeout afterwards.

### Skip rule

```c
bool xbee_dump_is_dumpable(const xbee_at_entry_t *entry)
{
  if (entry == NULL) {
    return false;
  }
  if (xbee_at_table_validate_get(entry->id) != SL_STATUS_OK) {
    return false;                       // EXEC and WRITE_ONLY: 14 commands
  }
  if ((entry->flags & XBEE_AT_FLAG_MULTI_RESPONSE) != 0U) {
    return false;                       // ND, ED, VL, FS
  }
  if (entry->type == XBEE_AT_TYPE_SUBCOMMAND) {
    return false;                       // PY
  }
  return !is_action_command(entry->id); // DN, CB
}
```

Three derivable clauses plus a two-entry residue, so the rule stays correct if the table grows.

Excluding `XBEE_AT_FLAG_MULTI_RESPONSE` is principled, not a guess: the facade passes `NULL, NULL`
for the line and response callbacks on both multi-response paths in `send_via_api()` and
`send_via_cmd_mode()`. A multi-response command therefore cannot deliver its output through
`xbee_at_get()` at all, and both transports still run the collection window to completion, which
costs a fixed `XBEE_CMD_MODE_MULTILINE_TIMEOUT_MS` of 4000 ms for nothing.

| Command | Why it is skipped |
| --- | --- |
| `ND` Network Discover | Transmits a discovery broadcast and listens for `NT`, 2.5 s by default. An action, not a parameter |
| `ED` Energy Detect | Loops through every available channel (manual 5643 to 5652), occupying the radio |
| `VL` Version Long | Harmless on air, but 4 s for text the facade discards, and its 64-byte reply also risks overflowing `req.value[65]` in API mode |
| `FS` File System | Subcommand interpreter, and `CMD_MODE_ONLY` |
| `PY` MicroPython Command | Subcommand interpreter into the Python VM |
| `DN` Discover Node | Not flagged `MULTI_RESPONSE`, so no derived clause catches it. A blank parameter returns `ERROR`, but the manual's timing for the no-response case is `NT * 100` = 2.5 s, past both transports' 1000 ms timeout; a late reply would then arrive while the next command is in flight and desynchronise the reply stream for the rest of the dump. On success in Command mode it also exits Command mode (manual 5058 to 5060) |
| `CB` Commissioning Pushbutton | Passes `validate_get` because it is neither `EXEC` nor write-only, but it has no stored value (`Default N/A`) and `CB 4` is "equivalent to sending an RE" (manual 6357 to 6360). A bare `ATCB` is expected to return a harmless `ERROR`, but "expected" is not a basis for poking it on a production fixture |

The residue is a file-scope `static const uint16_t action_commands[] = { XBEE_AT_DN, XBEE_AT_CB };`
with a comment per entry.

The predicate lives in the application layer, not in `xbee_at_table`. A
`xbee_at_table_is_dumpable()` would be symmetric with `xbee_at_table_is_provisionable()`, but it
would bake this application's judgement about what is safe to poke at boot into a table shared by
the transports and the provisioning app, and it would make the `VL` question a library decision.
Keeping it in `xbee_dump_format.c` costs nothing in testability, because that file builds on the
host anyway.

### Value formatting

The caller supplies the buffer; the function never returns a shared static, for the same reason
`value_text()` in `xbee_provision.c` does not, and because it keeps the function pure for host
tests.

`value_text()` is deliberately **not** reused or promoted. It is hexadecimal only, with no type
decoding, and it abbreviates past 16 bytes, which would hide most of `FK` and every SRP verifier,
exactly the values a dump exists to show. `xbee_provision.c` is left untouched.

Sizing: the hard bound on any value is `XBEE_AT_VALUE_MAX` = 65, so 130 hexadecimal characters; the
longest decoration is `"%s (not printable)"`, giving 149. `XBEE_DUMP_VALUE_TEXT_CAP` of 160 never
truncates, so no ellipsis logic is needed.

Decode with `byte_util_be_to_u64()` first, so a Command mode reply with leading zeros stripped still
prints at the declared width. **Key off `entry->type` and `entry->max_len`, never `req.value_len`**:
that is what makes the output identical in API and Transparent mode.

`--specs=nano.specs` is in force (`cmake_gcc/xbee_provision.cmake`), so newlib-nano's printf has no
`long long` support. Use `unsigned long`, never `%llu`. This is why `report_module()` in
`xbee_provision.c` already prints the 64-bit serial as two `%08lX` halves.

| Type | Condition | Format | Arguments |
| --- | --- | --- | --- |
| any | `len == 0` | `"(no value)"` | - |
| `U8` | | `"%u (0x%02X)"` | `(unsigned)v` twice |
| `U16` | | `"%u (0x%04X)"` | `(unsigned)v` twice |
| `U32` | | `"%lu (0x%08lX)"` | `(unsigned long)v` twice |
| `U64` | `v >> 32 == 0` | `"%lu (0x%08lX)"` | `(unsigned long)v` twice |
| `U64` | otherwise | `"0x%08lX%08lX"` | high half, low half |
| `BITMAP` | `max_len` 1 / 2 / >= 4 | `"0x%02X"` / `"0x%04X"` / `"0x%08lX"` | as above |
| `STRING`, `SUBCOMMAND` | every byte 0x20 to 0x7E | `"\"%s\""` | NUL-terminated local copy |
| `STRING`, `SUBCOMMAND` | otherwise | `"%s (not printable)"` | `byte_util_hex_encode()` |
| `BYTES` | | `"%s"` | `byte_util_hex_encode()` |

A `U64` cannot be printed in decimal above 2^32 under nano.specs without either pulling in the full
printf, costing several kB of flash, or hand-writing a 64-to-decimal converter. The three `U64`
commands are `US` (range caps at 0xFFFFFFFF), `IA` (an address) and `D%` (a packed manufacturing
date); decimal is meaningless for the two that can exceed 32 bits. Hexadecimal only above 2^32 is
recorded here as a decision rather than left as a surprise.

Two traps to guard: `req.value` is **not** NUL-terminated, so it must never be handed to `%s`
directly; copy into a bounded local and terminate it. And the printable test, which requires every
byte to be 0x20 to 0x7E, also prevents an embedded `0x00` from truncating a `%s`.

`command_name()` in `xbee_provision.c` returns a pointer into a shared `static char name[4]`, so two
calls in one log statement alias each other. It is not imported; the caller uses a local buffer:

```c
char mnemonic[4];
char text[XBEE_DUMP_VALUE_TEXT_CAP];

if (xbee_at_id_to_str(entry->id, mnemonic, sizeof(mnemonic)) != SL_STATUS_OK) {
  mnemonic[0] = '?';
  mnemonic[1] = '\0';
}
APP_LOG_INFO("%s (%s) = %s", mnemonic, xbee_at_table_name(entry),
             xbee_dump_format_value(entry, req.value, req.value_len,
                                    text, sizeof(text)));
```

### Output

```
[   0.012] I dump: XBee AT parameter dump: 147 commands in the table, 126 readable
[   1.480] I dump: module 0013A200 41B2C3D4, firmware 0x2003, hardware 0x2E46, in API 1 mode
[   1.492] I dump: max payload 100 bytes, output options 2, GT 1000 ms, CC 0x2B, CT 10.0 s
[   1.500] I dump: --- Networking ---
[   1.516] I dump: CH (Operating Channel) = 12 (0x0C)
[   1.532] I dump: ID (Extended PAN ID) = 13106 (0x3332)
[   2.140] I dump: --- Discovery ---
[   2.156] I dump: NI (Node Identifier) = " "
[   2.190] I dump: ND (Network Discover) not read: multi-response action command
[   6.880] W dump: P5 (DIO15/SPI_MISO Configuration) not readable, status 0x000B
[   7.020] I dump: dump complete: 124 read, 2 not readable, 21 not parameters, 7 skipped
[   7.034] I dump: elapsed 7012 ms, serial link: 1893 received, 812 sent, 0 overruns, 0 errors
```

- Log tag `"dump"`, set with `#define APP_LOG_TAG "dump"` before the `app_log.h` include.
- The header block comes from `xbee_get_info()` and `xbee_get_mode()`. The four-line `mode_name()`
  helper is copied from `xbee_bringup.c` rather than cross-included: application modules do not
  include each other.
- Category names are a file-scope `static const char *const category_names[XBEE_AT_CAT_COUNT]` in
  `xbee_dump_format.c`, in enum order, with a `_Static_assert` that the array covers
  `XBEE_AT_CAT_COUNT`. An out-of-range index returns `"unknown"` rather than indexing past the end.
- The seven skipped action commands get one line each with the reason: they are what a reviewer will
  ask about. The 21 `EXEC` and write-only exclusions are counted, not listed.
- Unreadable parameters log at warning level with the numeric status, so a timeout is
  distinguishable from a module `ERROR`.
- The footer carries the counts, the elapsed milliseconds and `xbee_uart_get_stats()`. The overrun
  count is the number that says whether to trust the dump at all.
- No icons. The `---` around headings is plain ASCII punctuation.

### Failure handling

**One bad read must never abort the dump, and that is structural rather than incidental: there is no
`finish()` call anywhere in the read path.** The only transition out of `DUMP_READ` is "table
exhausted". This needs an explicit comment, because the provisioning app it is modelled on does the
opposite, closing the session on a dispatch failure.

Dispatch failures:

- `SL_STATUS_NOT_READY` or `SL_STATUS_BUSY`: do not advance. Arm
  `stall_deadline = deadline_from_ms(XBEE_DUMP_STALL_TIMEOUT_MS)` on the first occurrence and retry
  on the next tick; when the deadline passes, log the entry as unreadable and advance. This is the
  Transparent mode case where the transport is re-entering Command mode underneath.
- Any other status: log, count as unreadable, advance immediately.

`table_index` advances monotonically, so the walk always terminates.

### Application selection

`app.c` gains `#define XBEE_APP_DUMP 3`, an `#include "xbee_dump.h"` and an `#elif` arm in
`app_init()`.

`app_process_action()` is a latent defect that must be fixed first. It currently reads
`#if PROVISION ... #else xbee_bringup_process(); #endif`, so a third `XBEE_APP` value would compile
cleanly and run *bring-up* while `app_init()` had started the dump: a silently half-working image.
It becomes a full `#if / #elif / #elif / #else #error` chain, symmetric with `app_init()`, which
already has the `#error`.

`cmake_gcc/CMakeLists.txt` gains the two new sources and `XBEE_AT_TABLE_NAMES=1`. Include paths need
no change: `../src/app/inc` is already on both the `xbee_provision` target and the generated `slc`
target. `XBEE_APP` stays `XBEE_APP_PROVISION` in both blocks; building the dump means changing both,
exactly as for `XBEE_APP_BRINGUP` today, and the comment above it records that pairing.

### Why XBEE_AT_TABLE_NAMES must be defined globally

The macro does not merely add strings: it changes the layout of `xbee_at_entry_t`, because
`const char *name;` sits inside the `#if XBEE_AT_TABLE_NAMES` in `xbee_at_table.h`. If one
translation unit in the image compiles with it and another without, the two disagree about
`sizeof(xbee_at_entry_t)`, and `xbee_at_table_at()` returns a pointer that the caller strides and
dereferences with the wrong layout. There is no link error and no warning, only garbage
`category`, `type` and `flags` bytes and a bogus `name` pointer. C offers no cross-translation-unit
guard against this.

Today `app.c` does not include `xbee_at_table.h`, so a target-scoped define would happen to be safe.
It is one `#include` away from not being. Defining it on both targets, always, removes the failure
mode instead of documenting it.

## Simplicity Studio changes required (PRIME RULE)

| Tool | Component / instance | Setting | Current value | Required value |
| --- | --- | --- | --- | --- |

None. This is application code plus `cmake_gcc/CMakeLists.txt` only. No component, clock,
peripheral, pin or energy-mode configuration changes.

## Resource impact

- **Flash**, measured with `arm-none-eabi-size` on the built images rather than estimated:

  | Build | `.text` | Note |
  | --- | --- | --- |
  | `XBEE_APP_BRINGUP` | 50 516 | Smallest variant |
  | `XBEE_APP_PROVISION`, `XBEE_AT_TABLE_NAMES=0` | 51 692 | The tree before this change |
  | `XBEE_APP_PROVISION`, `XBEE_AT_TABLE_NAMES=1` | 55 700 | **What ships by default** |
  | `XBEE_APP_DUMP`, `XBEE_AT_TABLE_NAMES=1` | 53 188 | |

  The provisioning build grows by **4008 bytes**, all of it the AT command names, which is more than
  the 2.6 kB estimated above: the linker keeps every name because the `at_table[]` initialiser
  references it, and the strings are longer on average than assumed. It remains 0.4 % of the
  1024 kB part and changes no behaviour. The dump build comes out 2512 bytes below the provisioning
  build, because `--gc-sections` drops the 108-entry provisioning table and `xbee_provision.c` from
  it.
- **RAM (.bss)**: 261 668 bytes in all four builds above, unchanged to the byte. The dump's roughly
  230 bytes of statics, one `xbee_at_req_t` plus the state, index, four counters and two tick
  values, displace the provisioning application's own statics rather than adding to them, and the
  figure is dominated by the SDK's heap and stack regions either way.
- **Stack**: a 160-byte text buffer, a 4-byte mnemonic buffer and one nano `vfprintf` frame of
  roughly 150 to 200 bytes, so about 400 bytes at peak, layered under `app_log_write()`'s own
  printf. `xbee_provision.c` already reaches a comparable depth with two `VALUE_TEXT_CAP` buffers
  plus printf, so this is not a new worst case. Recorded here so that nobody shrinks the stack
  afterwards.
- **Peripherals**: unchanged. EUSART0 for the XBee at 9600 through UARTDRV instance `XBEE`, EUSART2
  for VCOM at 115200 through iostream, the sleeptimer, and `XBEE_EN_GPIO` on PA00.
- **Energy**: both EUSARTs and the free-running super loop hold the device in EM0 throughout, as
  they already do. The dump extends EM0 by 6 to 8 s at boot. Afterwards the loop keeps spinning in
  EM0 driving `xbee_process()` with the module powered, where the module's own receive current
  dominates in any case. Powering the module down when finished was considered and rejected: the
  operator may want to attach XCTU to a powered board.

### Timing budget

9600 baud 8N1 is 1.042 ms per character; VCOM at 115200 is 86.8 µs per character.

| Phase | Command mode | API mode |
| --- | --- | --- |
| 126 reads, wire plus turnaround (about 15 ms and 25 ms each) | 1.9 s | 3.2 s |
| Long values (`FK` at 65 bytes, eight verifiers at 32 bytes) | 0.7 s | 0.4 s |
| 126 value lines over VCOM, about 65 characters each | 0.71 s | 0.71 s |
| Headings, header, footer and 7 skip lines | 0.2 s | 0.2 s |
| **Dump phase** | **3.5 s** | **4.5 s** |
| Bring-up (`XBEE_POWER_SETTLE_MS` 1000, probe, info reads) | 4.3 s | 1.4 s |
| **Boot to footer** | **8 s** | **6 s** |

The blocking VCOM writes cost roughly 20 to 25 % of wall-clock time, which is acceptable here for a
specific reason: nothing is in flight while the dump logs. The line is printed from
`handle_read_result()` after `xbee_at_req_complete()` and before the next `xbee_at_get()`, so a
5.6 ms stall, or 13 ms for `FK`, costs time but cannot lose bytes. Even if it did overlap, 9600 baud
fills the 512-byte RX ring at one byte per millisecond.

`CT` does not lapse during the dump. It defaults to 10 s and the facade passes it to the transport
through `xbee_cmd_mode_set_params()`. `refresh_session_deadline()` is called on every received reply
line inside `handle_line()`, as well as on entry, so at a 15 ms cadence the 9.5 s deadline (10 s
less `XBEE_CMD_MODE_CT_MARGIN_MS`) is pushed out some 630 times over the dump. The one residual: it
fires on received lines only, so about ten consecutive silent timeouts would let the session lapse;
the transport then re-enters on the next dispatch, which the 2000 ms stall window covers. That
deserves one comment in the code.

## Risks and open questions

| Risk | Handling |
| --- | --- |
| A command this module variant does not implement. The SPI category `P5` to `P9` is documented for the surface-mount and micro-mount parts, and the through-hole part remaps those pins | Warning line carrying the status, counted, dump continues. The same case `handle_read_result()` in `xbee_provision.c` already tolerates |
| Per-command timeout, 1000 ms in both transports | Same path. 126 timeouts is the worst case, 126 s: bounded, and the walk still terminates |
| `SL_STATUS_WOULD_OVERFLOW` from a value longer than `XBEE_AT_VALUE_MAX` | Reported through `req.result`; print that the value was too long to capture and continue. With `VL` skipped this should never fire |
| Secure session and SRP verifier reads (`*V` to `*Y`, `$V` to `$Y`) refused by firmware although the table does not mark them write-only | Unreadable path; not treated as a defect |
| The Command mode session lapses mid-dump | The transport re-enters on the next dispatch; the 2000 ms stall window is longer than `GT` plus the margins plus the `OK` |
| Bring-up fails outright | `XBEE_DUMP_RESULT_FAIL_BRINGUP`, one error line with `xbee_get_result()`, halt. No retry |
| Accidental write | Nothing in the module calls `xbee_at_set*`, `xbee_at_exec*`, `xbee_set_mode` or `xbee_hw_reset`. Grep for these as a review gate |

Open question, deliberately out of scope: capturing `VL` and `ND` properly would need a services
layer change to expose the multi-line callback through `xbee_at_req_t`. Worth doing if the long
version string or a node list is ever wanted in the dump; not worth 4 s each for output the facade
currently discards.

## Verification

### Host tests

Add `test_xbee_dump_format` to `test/CMakeLists.txt`, following the `test_xbee_provision_table`
block:

```cmake
add_module_test(test_xbee_dump_format
    "${PROJECT_ROOT}/src/app/src/xbee_dump_format.c"
    "${PROJECT_ROOT}/src/services/src/xbee_at_table.c"
    "${PROJECT_ROOT}/src/utils/src/byte_util.c")
target_include_directories(test_xbee_dump_format PRIVATE "${PROJECT_ROOT}/src/app/inc")
target_compile_definitions(test_xbee_dump_format PRIVATE XBEE_AT_TABLE_NAMES=1)
```

`src/app/inc` is not on the default `add_module_test()` include list; adding it per target is less
disruptive than changing the function. The `XBEE_AT_TABLE_NAMES=1` is mandatory, because both
sources in that executable must agree on the struct layout.

Cases:

1. **Category table.** Every index from 0 to `XBEE_AT_CAT_COUNT - 1` returns a non-empty string that
   is not `"unknown"`; an out-of-range index returns `"unknown"`.
2. **Skip rule.** False for all 13 `EXEC` identifiers, for `KY`, and for `ND DN ED VL FS PY CB`;
   true for `CH SH NI FK *V D% P5`. Walk the whole table and assert the dumpable count is exactly
   126: that is the regression tripwire if anyone edits the AT table.
3. **Formatter**, one assertion per row of the table above, including `U8` 0x0C giving
   `"12 (0x0C)"`; `U32` `SH` bytes `00 13 A2 00` giving `"1287168 (0x0013A200)"`; `U64` `IA` all
   0xFF giving `"0xFFFFFFFFFFFFFFFF"`; `BITMAP` `PR` with `max_len` 4 giving `"0x0000FFFF"`; a
   `U16` delivered as a single byte, the Command mode leading-zero case, still printing at
   `0x____` width; a `STRING` containing `0x01` giving hexadecimal plus `"(not printable)"`; and
   `len == 0` giving `"(no value)"`.
4. **Bounds.** Call with `cap` deliberately too small, say 4, with sentinel bytes past the end;
   assert nothing is written past `cap` and that the return is sane. Same for `entry == NULL` and
   for `value == NULL` with `len > 0`.

Run with:

```sh
cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure
```

### Build checks

`cd cmake_gcc && cmake --workflow --preset project` for each of `XBEE_APP_PROVISION`,
`XBEE_APP_BRINGUP` and `XBEE_APP_DUMP`. The third proves the `app.c` `#elif` chain and the two-place
define mirror. Any compiler warning is a defect. Compare the three `.bin` sizes against the flash
estimate above.

### On target

Application logging goes to VCOM on PD08/PD07 at 115200 only. There is no RTT backend in this
project, so J-Link RTT is not the channel for the dump, although the J-Link tooling remains the way
to flash, reset and halt. Capture VCOM to a file and check:

- 22 category headings in manual order, 126 value lines, 7 skip lines with reasons.
- `SH` and `SL` match the serial number printed on the module, and `AP` matches the mode the header
  block reported. A mismatch there means mode detection lied.
- `CK` (Configuration CRC) is identical across two consecutive runs: proof the dump changed nothing.
- The dump diffed against an XCTU profile read from the same module.
- The footer shows `0 overruns, 0 errors`, and an elapsed time within the budget above.
- **Run once in API mode and once with the module forced to `AP=0`, and confirm the two dumps are
  identical apart from timestamps.** That is the test that the type-driven formatter really is
  transport independent.
