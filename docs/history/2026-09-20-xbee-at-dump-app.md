# XBee AT parameter dump application

Date: 2026-09-20
Related plan: docs/plan/2026-09-20-xbee-at-dump-app.md

## Summary

Added a third application variant, `XBEE_APP_DUMP`, that brings the module up and logs every
readable AT parameter it reports: 126 parameters decoded to the type the command table records,
named, and grouped under the manual's 24 command categories. It writes nothing to the module. The
provisioning application and its logs are unchanged, and provisioning remains the default build.

## Motivation

The provisioning application prints a provisioning transcript rather than a configuration: one
identity line, then only the parameters that differ from `xbee_provision_config.h`. A parameter that
already matches is dropped deliberately, in the `// Matches.` branch of `handle_read_result()` in
`src/app/src/xbee_provision.c`. That is right for provisioning and useless for inspecting a board,
which until now needed XCTU.

## Changes

| File | Change |
| --- | --- |
| `app.c` | Added `XBEE_APP_DUMP`, its `#include` and its `app_init()` arm. **Fixed a latent defect in `app_process_action()`**: it selected provisioning with `#if` and *everything else* with `#else`, so a third `XBEE_APP` value would have compiled cleanly and run bring-up's process function while `app_init()` had started the dump. It is now a full `#if / #elif / #elif / #else #error` chain, symmetric with `app_init()`, which already had the `#error` |
| `src/app/inc/xbee_dump.h` | New. `xbee_dump_init()`, `xbee_dump_process()`, `xbee_dump_is_finished()`, `xbee_dump_get_result()` |
| `src/app/src/xbee_dump.c` | New. Four-state walk of the command table: `DUMP_IDLE`, `DUMP_BRINGUP`, `DUMP_READ`, `DUMP_DONE`. One request in flight, driven from the super loop |
| `src/app/inc/xbee_dump_config.h` | New. `XBEE_DUMP_POWER_CYCLE` (default 1) and `XBEE_DUMP_STALL_TIMEOUT_MS` (default 2000) |
| `src/app/inc/xbee_dump_format.h` | New. `xbee_dump_is_dumpable()`, `xbee_dump_category_name()`, `xbee_dump_format_value()`, and `XBEE_DUMP_VALUE_TEXT_CAP` |
| `src/app/src/xbee_dump_format.c` | New. The skip rule, the 24 category headings with a `_Static_assert` on the count, and the type-driven value renderer. Hardware independent |
| `test/test_xbee_dump_format.c` | New. Ten cases over the skip rule, the category table, every rendering branch, the capacity bound and the longest value in the command set |
| `test/CMakeLists.txt` | Registered `test_xbee_dump_format`, with `src/app/inc` on its include path and `XBEE_AT_TABLE_NAMES=1` |
| `cmake_gcc/CMakeLists.txt` | Added the two new sources. Added `XBEE_AT_TABLE_NAMES=1` to **both** definition blocks. Extended the `XBEE_APP` comment to name the new variant and to say that it must be changed in both blocks |

### Decisions worth recording

**`XBEE_AT_TABLE_NAMES` is defined on every target, not only where the dump needs it.** The macro
does not merely add strings: the `name` member of `xbee_at_entry_t` is declared inside its `#if`, so
two translation units compiled with different values of it disagree about `sizeof(xbee_at_entry_t)`,
and an entry read across that boundary is silently misinterpreted with no warning and no link error.
`app.c` does not include `xbee_at_table.h` today, so a target-scoped define would happen to be safe,
but only until someone adds that include. The comment in `CMakeLists.txt` records the reasoning.

**Seven commands that the command table reports as readable are not asked for**, because they act
rather than answer. Four are caught by `XBEE_AT_FLAG_MULTI_RESPONSE` (`ND`, `ED`, `VL`, `FS`), one
by its `XBEE_AT_TYPE_SUBCOMMAND` type (`PY`), and two had to be named explicitly:

- `DN` is not flagged multi-response. The manual gives the no-response window as `NT * 100` ms
  (lines 5062 to 5075), 2.5 s at the default `NT`, which is past both transports' one-second
  timeout; a reply that late would arrive while the next command was in flight and desynchronise the
  rest of the walk. A successful `DN` in Command mode also leaves Command mode (lines 5058 to 5060).
- `CB` passes `xbee_at_table_validate_get()`, has no stored value (`Default N/A`), and its parameter
  4 is "equivalent to sending an RE (Restore Defaults)" (lines 6357 to 6360). A bare `ATCB` is
  expected to answer `ERROR` and nothing more, but a read-only application has no reason to find
  out on a module it is meant to leave untouched.

The predicate lives in the application layer rather than beside `xbee_at_table_is_provisionable()`,
because it is a judgement about what is safe to ask a module at boot on a fixture, not a property of
the command set, and the transports and the provisioning application should not inherit it. Putting
it in `xbee_dump_format.c` costs nothing in testability, since that file builds on the host.

**No Command mode session is opened.** `xbee_at_get()` works in whatever mode bring-up found. On a
Transparent mode module the facade opens and maintains a session itself; on an API mode module a
forced session would cost a guard time of silence plus the wait for `OK` and buy nothing, because
the one `CMD_MODE_ONLY` command in the table is skipped. API framing also decodes per-command status
bytes into distinct `sl_status_t` values, where Command mode collapses every failure to a bare
`ERROR`.

**A failed read never stops the dump.** There is no `finish()` call anywhere in the read path; the
only way out of `DUMP_READ` is the end of the table. This is deliberately unlike the provisioning
application, where a failed read must stop the run, and it is commented as such in the source.

## Simplicity Studio changes (made by the user)

| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |

None. No component, clock, peripheral, pin or energy-mode configuration changed. The dump uses the
EUSART0 XBee link, the EUSART2 VCOM log and `XBEE_EN_GPIO` on PA00 exactly as they are already
configured.

## Verification

**Build: passed.** All three application variants build without a single compiler warning, with
`cmake --workflow --preset project` from a clean `build/` directory each time. Measured with
`arm-none-eabi-size`:

| Build | `.text` | `.bss` |
| --- | --- | --- |
| `XBEE_APP_BRINGUP` | 50 516 | 261 668 |
| `XBEE_APP_PROVISION`, names off (the tree before this change) | 51 692 | 261 668 |
| `XBEE_APP_PROVISION`, names on (what ships) | 55 700 | 261 668 |
| `XBEE_APP_DUMP`, names on | 53 188 | 261 668 |

The provisioning build grows by 4008 bytes, all of it the AT command names. **That is more than the
plan estimated**, which was about 2.6 kB: the linker keeps every name because the `at_table[]`
initialiser references it, and the strings are longer on average than assumed. It is 0.4 % of the
1024 kB part and changes no behaviour. `.bss` is unchanged to the byte in every variant.

**Host tests: passed.** All 9 suites, 100 %, under `-Wall -Wextra -Werror`. The new
`test_xbee_dump_format` contributes 99 checks, including an assertion that the skip rule admits
exactly 126 of the 147 commands, which is the tripwire if anyone edits the AT command table.

One test expectation was wrong on the first run and was corrected: 0x0013A200 is 1286656, not
1287168. The renderer was right.

**Static review: passed.** `grep -nE "xbee_at_set|xbee_at_exec|xbee_set_mode|xbee_hw_reset|xbee_cmd_session_open|xbee_deinit"`
over both new sources matches only the comment in `xbee_dump.c` that asserts their absence. The dump
issues no write, no exec, no mode change, no reset and no forced session.

**On target: not done.** Nothing in this change has run on hardware.

## Known limitations and follow-ups

- **The on-target checks in the plan are outstanding.** In particular: that the 22 category headings
  and 126 value lines appear as expected; that `CK` (Configuration CRC) is identical across two
  consecutive runs, which is the proof the dump changed nothing; and that a dump taken in API mode
  and one taken with the module forced to `AP=0` are identical apart from timestamps, which is the
  test that the type-driven renderer really is transport independent.
- The predicted 6 to 8 s boot-to-footer time is calculated from the baud rates and the per-command
  turnaround, not measured. The footer prints the elapsed time, so the first run on hardware
  settles it.
- `VL` and `ND` cannot be captured at all today, whatever the dump does: the facade passes `NULL`
  for the collecting callbacks on both multi-response paths, so the answer is discarded after the
  full 4 s collection window. Reporting the long version string or a node list would need a services
  layer change to expose that callback through `xbee_at_req_t`. Not attempted here.
- A `U64` above 2^32 prints in hexadecimal only. newlib-nano's printf has no `long long`
  conversion, and the two commands that can exceed 32 bits hold an address and a packed
  manufacturing date, where a decimal form means nothing.
