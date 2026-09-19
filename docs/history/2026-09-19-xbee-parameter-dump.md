# Boot-time dump of every XBee parameter the module will report

Date: 2026-09-19
Related plan: docs/plan/2026-09-19-xbee-parameter-dump.md

## Summary

Added `xbee_dump`, a standalone application-layer module that reads every AT
parameter the module is willing to report and logs one line per parameter, right
after bring-up, in both the provisioning and the bring-up build. Measured on the
command table as it stands: **127 of 147 commands are read, 20 are skipped.**

Removed the boot log lines that did nothing but restate a value the dump now
prints, so no parameter appears twice in one boot log.

## Motivation

A boot log could not answer "what is on this module". `xbee_bringup` reported
the nine parameters the facade reads during bring-up and nothing else, and the
provisioning audit reads all 108 provisionable parameters but logs only those
that differ from the configured value, so a module that already matches produced
no per-parameter output at all.

The dump is a separate module rather than code inside either application because
a later build preset will compile the provisioning application without
`src/app/src/xbee_bringup.c`, and the dump has to survive that.

## Changes

| File | Change |
| --- | --- |
| `src/app/inc/xbee_dump.h` | New. Public API: start, process, is finished, result, and the shared credential predicate |
| `src/app/inc/xbee_dump_config.h` | New. The two bounds, the session timeouts and the value abbreviation threshold, all `#ifndef` guarded |
| `src/app/src/xbee_dump.c` | New. The state machine, the exclusion predicate, the per-parameter log lines and the summary |
| `src/app/src/xbee_bringup.c` | New `BRINGUP_DUMPING` state; `report_success()` no longer restates parameters |
| `src/app/src/xbee_provision.c` | New `PROV_DUMP` state between `PROV_OPEN` and `PROV_AUDIT`; `report_module()` shrunk to a mode-only line; `command_name()` and `value_text()` reworked (see the credential masking entry) |
| `src/app/inc/xbee_provision_config.h` | New `XBEE_PROV_DUMP`, default 1 |
| `src/services/src/xbee.c` | `"detected %s mode, AP=%u"` becomes `"detected %s mode"` |
| `src/utils/inc/byte_util.h`, `src/utils/src/byte_util.c` | New `byte_util_hex_text()`, the log formatting promoted out of `xbee_provision.c` |
| `test/test_byte_util.c` | New `test_hex_text` suite, 14 checks |
| `cmake_gcc/CMakeLists.txt` | Registers `xbee_dump.c`; defines `XBEE_AT_TABLE_NAMES=1` on both the `xbee_provision` and the `slc` target |

### Which parameters are dumped

`dump_wanted()` excludes, in this order, and the counts are measured against the
table rather than estimated:

| Exclusion | Commands | Count |
| --- | --- | --- |
| `xbee_at_table_validate_get()` refuses it | `KY`, `AS`, `DA`, `FP`, `CN`, `IS`, `%P`, `FR`, `AC`, `WR`, `RE`, `%F`, `!C`, `R1` | 14 |
| `XBEE_AT_FLAG_MULTI_RESPONSE` | `ND`, `ED`, `FS`, `VL` | 4 |
| `XBEE_AT_TYPE_SUBCOMMAND` | `PY` | 1 |
| `XBEE_AT_DN` | `DN` | 1 |

`FS` is excluded by the multi-response flag before the subcommand check reaches
it, so the subcommand exclusion accounts for `PY` alone.

`KY` is excluded by the first rule but still gets a line of its own,
`KY (AES Encryption Key) = (write only, 16 bytes)`, so the log is honest that a
key exists.

### Command mode session ownership

Decided once, in `xbee_dump_start()`:

1. A session is already open, the provisioning path: adopted and left open.
2. Transparent mode with no session: one is opened and closed by the dump.
3. An API mode with no session: none is opened.

Case 3 is the one worth recording. `use_cmd_transport()` in
`src/services/src/xbee.c` routes every request through Command mode whenever a
session is forced, regardless of the detected mode, so forcing one in an API
mode would have added the roughly 2.1 second entry cost and then made each of
the 127 reads slower rather than faster.

Case 1 relies on `handle_line()` in `src/services/src/xbee_cmd_mode.c` returning
the transport to `ACTIVE` rather than `IDLE`, and on
`refresh_session_deadline()` pushing the deadline to CT minus the margin, 9.5 s,
on every accepted line. Back-to-back reads therefore pay the entry cost once.

### Two independent bounds

- `XBEE_DUMP_MAX_CONSECUTIVE_TIMEOUTS`, default 3. The facade's per-request
  timeout is 1000 ms, so a module that has stopped answering ends the dump after
  roughly 3 seconds instead of spending 127 seconds on the remaining commands.
- `XBEE_DUMP_TOTAL_TIMEOUT_MS`, default 30000. Checked at the top of every tick,
  so a link that answers slowly but not never is still bounded.

The two are reachable independently: the first by a dead link, the second by a
slow one that keeps answering.

### The bound never abandons a request in flight

Not in the plan as written, and found while reviewing the failure paths. The
facade holds exactly one request slot. If a bound fired while a read was in
flight and the dump simply stopped, that slot would stay taken and the next
`xbee_at_get()` from the provisioning sequence would be rejected with
`SL_STATUS_BUSY`, turning a diagnostic timeout into a provisioning failure.

An expired bound therefore sets a flag, and the dump stops only once the
in-flight read has completed. That wait is itself bounded, by the facade's own
per-request timeout.

### Log lines removed

Each of these restated a value the dump now prints in full:

- `xbee_bringup.c`, `report_success()`: `"serial number %08lX%08lX"` (SH, SL),
  `"firmware 0x%04X, hardware 0x%04X"` (VR, HV) and
  `"max payload %u bytes, output options %u"` (NP, AO).
- `xbee_provision.c`, `report_module()`: the serial number and firmware fields.
  The line survives as a mode-only phase marker.
- `xbee.c`: the `AP=%u` field of `"detected %s mode, AP=%u"`.

Deliberately kept, because none of them is a parameter restatement: the
`"module ready in <mode> mode"` and `"ready in <mode> mode"` phase markers, the
AO=2 legacy-frames WARNING (a judgement about a value, not a listing of it), the
serial link statistics and the overrun warning (driver counters, not AT
parameters), the audit's `"%s is %s, configured as %s"` line (a comparison
against the configured value, which the dump does not make), and every WARNING
and ERROR line.

### XBEE_AT_TABLE_NAMES on two targets

`XBEE_AT_TABLE_NAMES` adds a `const char *name` member to `xbee_at_entry_t`
under an `#if` (`src/services/inc/xbee_at_table.h`). `app.c` is compiled in the
generated `slc` object library, which does not inherit the `xbee_provision`
target's definitions, so the macro is defined on both targets. Defining it on
one only would be a silent disagreement about the size of the structure, with no
diagnostic from the compiler or the linker. The reason is written next to the
definition in `cmake_gcc/CMakeLists.txt`.

## Simplicity Studio changes (made by the user)

**None, and none were needed.** The dump uses only existing application and
service code: the facade, the AT command table, the sleeptimer and `app_log`
over the existing VCOM iostream instance. No component, pin, clock or peripheral
changed, and no file under `config/`, `autogen/` or the generated `cmake_gcc/`
files was touched. `git status` shows no Studio-owned file modified.

## Verification

**Build, host tests and static review. Nothing was run on hardware.**

- Host tests: 8 suites, all passing. `test_byte_util` now makes 82 checks, up
  from 68. The new cases were confirmed to fail on a wrong expectation rather
  than passing vacuously.
- Target build clean, zero warnings, in both application selections
  (`XBEE_APP_PROVISION` and `XBEE_APP_BRINGUP`).
- Image size, from `arm-none-eabi-size` and
  `cmake_gcc/build/base/xbee_provision.map`:

  | Build | text | Delta |
  | --- | --- | --- |
  | Before | 51 740 | |
  | With the dump, `XBEE_AT_TABLE_NAMES=0` | 54 884 | +3 144 |
  | With the dump, `XBEE_AT_TABLE_NAMES=1`, as shipped | 58 892 | **+7 152** |
  | Bring-up selection, as shipped | 53 228 | |

  `.data` grew by 4 bytes. The `.bss` section grew from 0x1110 to 0x1294,
  **+388 bytes** of static RAM, which the linker takes out of `.heap`; that is
  why the `bss` column reported by `arm-none-eabi-size`, which includes the heap
  region, is unchanged.

- Static review of `xbee_dump_process()`: it issues at most one `xbee_at_get()`
  per call, and emits at most one parameter line per call. A completed read logs
  its line and returns; the next tick advances the table walk. The walk itself
  does no input or output, so a run of excluded commands costs no extra time,
  except that `KY`'s masked line ends the call. Only the call that ends the dump
  writes more than one line: the reason it stopped, if any, and the summary.
- Both bounds were traced to a reachable path, see above.
- The boot path was grepped for a remaining line that prints a value the dump
  also prints. None was found.

## Known limitations and follow-ups

- **Not run on hardware.** The timings quoted in the plan, roughly 1.3 seconds
  for 127 reads inside an open session, are calculated from 9600 baud and the
  frame sizes, not measured.
- In the provisioning build, `report_module()`'s
  `"module ready in <mode> mode"` sits close to the facade's own
  `"ready in <mode> mode"`. Both were kept as phase markers, which is what the
  design asked for, but they read as near-duplicates in a boot log and could be
  reduced to one.
- `xbee_bringup_passed()` returns false while the dump is running, because the
  sequence is not finished yet. Nothing in `app.c` consults it before
  `xbee_bringup_is_finished()`, so this is a note rather than a defect.
- The dump reads the module as found. It does not compare anything against the
  configuration; that remains the audit pass's job.
