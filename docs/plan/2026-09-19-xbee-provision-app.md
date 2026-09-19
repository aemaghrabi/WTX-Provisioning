# XBee provisioning application: header-driven configuration with one WR

Status: Done, pending on-target verification
Date: 2026-09-19
Related history: docs/history/2026-09-19-xbee-provision-app-phase1.md through
docs/history/2026-09-19-xbee-provision-app-phase4.md

All four phases are implemented, build without warnings and pass the host tests.
**None of it has been run on hardware.** The on-target checks listed under
"Phases and verification" below are outstanding.

Departures from the plan as written, all recorded in the phase histories:

- The facade's `xbee_cmd_session_close()` takes no request. It uses the
  transport's own exit rather than an ordinary exit command, because sending
  that command through the generic path would leave the transport believing a
  closed session was still open.
- `xbee_at_exec_timeout()` was added. The plan did not foresee that the facade
  applied one fixed one-second timeout to every request, which is too short for
  the commands that erase and rewrite flash.
- The host test reports the write set rather than asserting the header is all
  default. Asserting it would have failed the moment anyone configured the
  product, which is the header's purpose.
- The per-entry mismatch record was not needed. The audit only has to decide
  whether to provision at all; the write set is computed from the defaults.

## Context

The XBee driver stack (`docs/plan/2026-09-18-xbee3-driver-stack.md`) gives the application
mode-independent access to every AT command, but the only application on top of it is
`xbee_bringup`, which powers the module, detects its mode and logs what it found. The readme's
scope item 3, "XBee radio configuration ... persist it on the module (`WR`). Parameter set: TBD",
is still open.

This plan adds a second, independent application, `xbee_provision`, that at boot brings the module
to the configuration described in one header file and persists it with a single `WR`. The user's
requirements, with the decisions taken on 2026-09-19:

| Requirement / decision | Outcome |
| --- | --- |
| Header lists every XBee parameter, including AES | `src/app/inc/xbee_provision_config.h`: one macro per provisionable parameter, 108 in total, preloaded with the factory default from the manual, with `KY` (AES key), `EE`, secure session and Bluetooth SRP material included |
| Only parameters that deviate from the factory default are written | The write set is computed against the defaults recorded in `xbee_at_table` |
| One NVM write | Exactly one `WR` per provisioning run, after every parameter write |
| Independent from the existing app | New module `xbee_provision`; `app.c` selects one app with a build-time macro `XBEE_APP`, provisioning by default |
| Restore defaults first | Supported for both `R1` (Restore Factory Defaults) and `RE` (Restore Defaults); `R1` is the default. With `R1`, an `ERROR` reply falls back to `RE` with a warning |
| Check first, write only on mismatch | Every readable provisionable parameter is read and compared with the header before anything is written; a matching module is left alone, saving a flash cycle |
| Verify after WR | Hardware reset through the facade, then a full read-back and comparison |
| `KY` is write-only | The readable set is trusted: if every readable parameter matches, `KY` is assumed to match too. `KY` is written only when a provisioning run happens and the header key is non-zero |

## Background and references

All manual references are to `docs/manuals/xbee_90002273_ref_manual.md` (Digi XBee 3 802.15.4
RF Module User Guide, 90002273 R).

| Topic | Lines | What it establishes |
| --- | --- | --- |
| Command mode in every operating mode | 3041 | "Command mode is available on the UART interface for all operating modes", so `+++` works whether the module is in Transparent or API mode. Provisioning therefore always runs inside one Command mode session, whatever mode the module starts in |
| Changes do not apply until AC / CN / timeout | 3101 to 3110 | In Command mode a parameter write is staged, so writing `AP` or `BD` mid-session does not change the interface under the session |
| WR persists staged changes | 3105 to 3107, 7955 to 7958 | "Send WR. In this case, changes are only applied following a reset. The WR command by itself does not apply changes." A queued change can be "written to flash with a queued WR command" |
| RE then WR | 3113 to 3115 | "Send an RE followed by WR to restore parameters back to their factory defaults. The next time the device is reset the default settings are applied" |
| RE keeps the session | 7092 | "Does not exit out of Command mode" |
| RE honours custom defaults, R1 does not | 7091, 7107 to 7140 | `RE` restores "factory or custom-set defaults"; `R1` "Restores factory defaults, ignoring any custom defaults set using %F". Both are local-only |
| WR cost and etiquette | 7089 to 7099 | 10 000 erase/write cycles; "do not send any additional characters to the device until after you receive the OK response" |
| FR | 7063 to 7070 | `OK` at once, reset 100 ms later. Not used: the reset line is available and already driven by `xbee_hw_reset()` |
| KY write-only | 5449 to 5458 | "This command is write-only and cannot be read. If you attempt to read KY, the device returns an OK status." Default 0 |
| IA default | 6795 to 6803 | Default `0xFFFFFFFFFFFFFFFF`, a 64-bit value the current table cannot hold (see "Table corrections") |
| BD, NB, SB | 6060 to 6110 | Standard rates 0 to 0x0A, default 3 = 9600; parity 0 to 2; stop bits 0 to 1 |
| Command mode parameters | 6139 to 6179 | `CC`, `CT`, `GT` defaults, used by the existing transport |

Existing code that is reused, all verified in this session:

- `src/services/inc/xbee.h`: `xbee_init()`, `xbee_process()`, `xbee_is_ready()`,
  `xbee_get_state()`, `xbee_get_result()`, `xbee_get_mode()`, `xbee_get_info()`,
  `xbee_at_get()`, `xbee_at_set()`, `xbee_at_exec()`, `xbee_at_req_complete()`,
  `xbee_hw_reset()`. One request in flight at a time, which suits a sequential provisioner.
- `src/services/inc/xbee_at_table.h`: `xbee_at_table_find()`, `xbee_at_table_at()`,
  `xbee_at_table_count()`, `xbee_at_table_validate_set()`, `xbee_at_id_to_str()`, the
  `XBEE_AT_*` identifiers, the `XBEE_AT_TYPE_*` and `XBEE_AT_FLAG_*` values and
  `XBEE_AT_VALUE_MAX` (65).
- `src/services/src/xbee.c`: `dispatch_request()` (line 384) already routes a request to
  `send_via_cmd_mode()` when the detected mode is Transparent, opening the session on demand;
  `xbee_process()` (line 988) picks which transport to drive from `detected_mode`;
  `submit()` (line 831) validates against the table and refuses Command-mode-only commands in
  API modes. These are the three places the forced session touches.
- `src/services/inc/xbee_cmd_mode.h`: `xbee_cmd_mode_enter()`, `xbee_cmd_mode_exit()`,
  `xbee_cmd_mode_is_ready()`, `xbee_cmd_mode_get_state()`, `xbee_cmd_mode_abandon()`.
- `src/utils/inc/app_log.h`: `APP_LOG_INFO/WARNING/ERROR` with a per-file `APP_LOG_TAG`.
- `src/app/src/xbee_bringup.c`: the pattern for an app module (state enum, `_init()`,
  `_process()`, `_is_finished()`, `_passed()`) and its logging style.
- `test/CMakeLists.txt`: `add_module_test()`, `fake_platform.c`; `test/test_util.h` asserts.
- `config/sl_uartdrv_eusart_XBEE_config.h` (read-only): `SL_UARTDRV_EUSART_XBEE_BAUDRATE` 9600,
  `SL_UARTDRV_EUSART_XBEE_PARITY` `eusartNoParity`, `SL_UARTDRV_EUSART_XBEE_STOP_BITS`
  `eusartStopbits1`. The header's `BD`, `NB` and `SB` are checked against these at compile time.

## Design

### Layering

```
app.c  --XBEE_APP-->  src/app/xbee_provision        state machine: bring-up, audit, restore, write, WR, reset, verify
                        src/app/xbee_provision_table  const table built from xbee_provision_config.h (host-testable)
                        src/app/inc/xbee_provision_config.h   the 108 parameter values plus options
                     -> src/services/xbee            facade, gains a forced Command mode session
                     -> src/services/xbee_at_table   gains provisionable / default / equality helpers
```

No new driver. No Studio change.

### The configuration header: `src/app/inc/xbee_provision_config.h`

One `#ifndef`-guarded macro per provisionable parameter, grouped in the manual's category order,
each with a one-line comment giving the meaning, range, unit and manual lines, and preloaded with
the factory default so an untouched header provisions nothing.

Provisionable means, from the table: not `XBEE_AT_TYPE_EXEC`, not `XBEE_AT_TYPE_SUBCOMMAND`, not
`XBEE_AT_FLAG_READ_ONLY`, not `XBEE_AT_FLAG_VOLATILE`, not `XBEE_AT_FLAG_NO_DEFAULT`. That is
108 commands: CH ID MM C8; NI DD NT NO; CE A1 A2 SC SD; MY DH DL RR TO; EE KY DM US;
SA *S *V *W *X *Y; PL CA RN; SM SP ST DP SN SO; PS; FK; BT BI BP $S $V $W $X $Y; AP AO AZ;
BD NB SB FT RO; CC CT GT; D6 D7 P3 P4; P5 P6 P7 P8 P9; D0 D1 D2 D3 D4 D5 D8 D9 P0 P1 P2 PR PD M0
M1 RP LT; IR IC AV IT IF; IA IU T0 T1 T2 T3 T4 T5 T6 T7 T8 T9 Q0 Q1 Q2 PT; LX LY LZ.
Excluded, and listed in a comment block at the end of the header so the reader knows why: read-only
(SH, SL, NP, AI, PP, DB, BL, VR, VL, VH, HV, R?, %C, %V, TP, CK, D%), executable (ND, DN, AS, DA,
ED, FP, IS, CB, CN, FR, AC, WR, RE, %F, !C, R1, %P), subcommands (PY, FS), volatile counters
(EA, EC) and IO (an output action, no stored default).

Three value forms:

```c
#define XBEE_PROV_CH   0x0CU                     // integer, width from the table
#define XBEE_PROV_NI   " "                       // string, 1 to 20 printable characters
#define XBEE_PROV_KY   { 0x00, 0x00, ... }       // bytes, exactly the table width (16)
```

Integers are plain values in the width the table gives (U8/U16/U32/U64/BITMAP). Strings are
string literals. Byte parameters (`KY` 16, `*V..*Y` 32 each, `$V..$Y` 32 each, `FK` 65) are
initialiser lists of exactly the table width; all zero is the factory default.

Options, also `#ifndef`-guarded:

| Macro | Default | Meaning |
| --- | --- | --- |
| `XBEE_PROV_RESTORE` | `XBEE_PROV_RESTORE_R1` | `XBEE_PROV_RESTORE_R1` or `XBEE_PROV_RESTORE_RE`: which restore command precedes the writes. With `R1`, an `ERROR` reply falls back to `RE` and logs a warning |
| `XBEE_PROV_POWER_CYCLE` | 1 | Power cycle the module at start, as bring-up does |

Compile-time consistency checks in `xbee_provision_table.c`: `XBEE_PROV_BD` must be the code for
`SL_UARTDRV_EUSART_XBEE_BAUDRATE` (3 for 9600, 4 for 19200, 5 for 38400, 6 for 57600, 7 for
115200; manual 6076 to 6086), `XBEE_PROV_NB` must be 0 (`eusartNoParity`) and `XBEE_PROV_SB` 0
(`eusartStopbits1`). Otherwise the post-write verification could not talk to the module, and the
main firmware would inherit a link the Studio configuration does not match. Changing the product's
baud rate is then a Studio change on the `XBEE` instance plus a header change, made together.
`XBEE_PROV_AP` must be 0, 1 or 2 (the facade does not drive AP=4).

### The compiled table: `src/app/src/xbee_provision_table.c`, `src/app/inc/xbee_provision_table.h`

The parameter list is an X-macro in a private header next to the source,
`src/app/src/xbee_provision_list.h`:

```c
#define XBEE_PROV_LIST(INT, STR, BYTES)  \
  INT(CH, XBEE_AT_CH, 1)                 \
  INT(ID, XBEE_AT_ID_, 2)                \
  ...                                    \
  STR(NI, XBEE_AT_NI)                    \
  BYTES(KY, XBEE_AT_KY, 16)              \
  ...
```

`xbee_provision_table.c` expands it twice: once to emit one `static const uint8_t` array per
parameter, already in wire form (integers big-endian at the table width through
`XBEE_PROV_BE8/16/32/64()` helper macros, strings as their characters without the terminator,
bytes as given), and once to emit the table:

```c
typedef struct {
  uint16_t       command;   // XBEE_AT_* identifier
  const uint8_t *value;     // wire form, big-endian for integers
  uint8_t        len;       // bytes
} xbee_prov_param_t;

const xbee_prov_param_t *xbee_provision_table_get(uint16_t *count);
```

Everything is `const`, so it lives in flash. This file depends only on `xbee_at_table.h`,
`sl_status.h` and the config header, so it builds on the host, where a test asserts that the list
covers exactly the provisionable entries of `xbee_at_table` (none missing, none extra), that every
entry's width equals the table's `max_len`, and that every header value passes
`xbee_at_table_validate_set()`. A mistyped header value therefore fails the host tests, not the
board.

### `xbee_at_table` additions (services, host-tested)

```c
bool xbee_at_table_is_provisionable(const xbee_at_entry_t *entry);
// Writable, stored, has a documented default: the rule in the previous section.

bool xbee_at_table_is_default(const xbee_at_entry_t *entry, const uint8_t *value, uint16_t len);
// Integers: decoded big-endian value == default_value. Strings: the single default character
// (0x20 for NI, BI, LX, LY, LZ). Bytes: every byte zero, or empty.

bool xbee_at_table_values_equal(const xbee_at_entry_t *entry,
                                const uint8_t *a, uint16_t alen,
                                const uint8_t *b, uint16_t blen);
// Integers compare numerically, so a value the module reports with fewer leading zero bytes
// (Command mode prints hex without padding) still matches. Strings and bytes compare exactly.
```

### Table corrections

`default_value` widens from `uint32_t` to `uint64_t`. The manual gives `IA` a default of
`0xFFFFFFFFFFFFFFFF` (line 6803), which the current 32-bit field stores as `0xFFFFFFFF`, so a
header that leaves `IA` at its documented default would be miscounted as a deviation, and the
comparison against a read-back would fail. Cost: 4 bytes per entry and 8-byte alignment, about
0.6 kB of flash across 147 entries. `US` (default 0) is unaffected. The existing table test that
checks known defaults is extended with `IA`.

### Facade addition: a forced Command mode session

Provisioning always runs in Command mode, because in Command mode nothing applies until the
session ends (manual 3101 to 3110), `RE`/`R1` keep the session (7092), and `WR` persists the
staged values (3105 to 3107). That is also the only documented way to restore defaults without the
`AP` value being applied at once, which in API mode would drop the transport mid-run. The facade
currently opens a session only when the detected mode is Transparent, so it gains:

```c
sl_status_t xbee_cmd_session_open(void);
// Ready state only. Forces every subsequent request through the Command mode transport,
// whatever mode was detected, and starts the entry sequence. Returns at once.

sl_status_t xbee_cmd_session_status(void);
// SL_STATUS_IN_PROGRESS while entering, SL_STATUS_OK while open,
// SL_STATUS_TIMEOUT if the module never answered the escape sequence (the force is
// then released), SL_STATUS_INVALID_STATE when no session was requested.

sl_status_t xbee_cmd_session_close(xbee_at_req_t *request);
// Sends CN. When it completes, routing returns to the detected mode. Leaving Command
// mode applies staged changes (manual 3101 to 3110), so the provisioner only calls this
// on the "nothing to write" path and after verification.
```

Implementation in `xbee.c`: a `static bool cmd_session_forced`. `dispatch_request()` uses the
Command mode path when `cmd_session_forced || !mode_is_api(detected_mode)`; `xbee_process()`
drives only `xbee_cmd_mode_process()` while forced, because both transports read from the same
UART ring and would otherwise steal each other's bytes; `submit()` skips the
Command-mode-only refusal while forced. If the module's own `CT` timeout closes the session
(transport back to `XBEE_CMD_STATE_IDLE` without a close request), the force is released and the
routing returns to the detected mode, so a stalled provisioner cannot leave the facade deaf.
`xbee_hw_reset()` and `xbee_deinit()` clear the flag through the existing
`xbee_cmd_mode_abandon()` path.

`XBEE_CMD_MODE_LINE_MAX` in `src/services/inc/xbee_cmd_mode_config.h` rises from 128 to 160: a
read of `FK` answers with 130 hexadecimal characters, which the current line buffer would
truncate. The command buffer (`XBEE_CMD_MODE_CMD_MAX`, 160) already covers the write.

### The application: `src/app/inc/xbee_provision.h`, `src/app/src/xbee_provision.c`

`APP_LOG_TAG "provision"`. Same shape as `xbee_bringup`: `xbee_provision_init()` from
`app_init()`, `xbee_provision_process()` from `app_process_action()`,
`xbee_provision_is_finished()`, `xbee_provision_passed()`, plus
`xbee_provision_get_result()` returning a small enum for a future reporting layer:
`PASS_ALREADY_PROVISIONED`, `PASS_WRITTEN`, `FAIL_BRINGUP`, `FAIL_SESSION`, `FAIL_RESTORE`,
`FAIL_WRITE`, `FAIL_COMMIT`, `FAIL_RESET`, `FAIL_VERIFY`.

State machine, driven from `xbee_provision_process()`, one facade request in flight at a time,
every wait bounded by the facade's own timeouts:

| State | Action | Next |
| --- | --- | --- |
| `BRINGUP` | `xbee_init({AUTO, power_cycle})`; wait for ready. Log mode, serial number, firmware | ready: `OPEN`; failed: `FAIL_BRINGUP` |
| `OPEN` | `xbee_cmd_session_open()`; wait for status OK | OK: `AUDIT`; timeout: `FAIL_SESSION` |
| `AUDIT` | For each table entry that is readable (not `KY`): `xbee_at_get()`, compare with `xbee_at_table_values_equal()`. Record mismatch per entry. A read the module answers with `ERROR` is logged as a warning and treated as not comparable (a pin command may not apply to this variant) | all match: `CLOSE` with `PASS_ALREADY_PROVISIONED`; any mismatch: `RESTORE` |
| `RESTORE` | `xbee_at_exec(R1)` (or `RE` per `XBEE_PROV_RESTORE`). With `R1`, `ERROR` falls back to `RE` once | OK: `WRITE`; failure: `FAIL_RESTORE` |
| `WRITE` | For each entry where `!xbee_at_table_is_default(value)`: `xbee_at_set()`. `KY` included here when its header value is non-zero. Any `ERROR` or timeout stops the run | done: `COMMIT`; failure: `FAIL_WRITE` |
| `COMMIT` | `xbee_at_exec(WR)`, the single flash write. Nothing else is sent until its `OK` (manual 7093 to 7095) | OK: `RESET`; failure: `FAIL_COMMIT` |
| `RESET` | `xbee_hw_reset()`: pulses the reset line, re-runs bring-up with AUTO detection. Wait for ready. Check `xbee_get_mode()` matches `XBEE_PROV_AP` | ready and mode matches: `OPEN` again with `verify = true`; otherwise `FAIL_RESET` |
| `VERIFY` | Same read loop as `AUDIT`. Any mismatch is a failure; the first is logged with command, expected and actual | all match: `CLOSE` with `PASS_WRITTEN`; mismatch: `FAIL_VERIFY` (still `CLOSE`, then fail) |
| `CLOSE` | `xbee_cmd_session_close()`; wait | `PASSED` or `FAILED` per the recorded result |

The mismatch record is one `uint8_t` per table entry (108 bytes, static). `AUDIT` and `VERIFY`
share one read routine parameterised by a `verify` flag. The write set is computed with
`xbee_at_table_is_default()` at run time, not stored.

Logging: at INFO, one line per phase, the counts (parameters read, mismatching, to be written),
each parameter written (command and value, hexadecimal; `KY` logged as "KY: 16 bytes" without
the key), the `WR` result, and the final verdict. At WARNING, unreadable parameters and the `R1`
fallback. At ERROR, the failing command, its status and the facade state.

Timing with defaults, worst case (module starts in Transparent mode, everything deviates):
bring-up about 4.2 s (settle 1 s, API probe 1 s, Command mode entry 2.1 s), session open 2.1 s,
audit about 3 s (107 reads at 9600 baud), restore plus writes plus `WR` under 3 s, reset and
bring-up about 4 s, second session 2.1 s, verify 3 s, close. About 20 s. A module that is already
provisioned finishes in about 10 s.

### `app.c` selection

```c
#include "xbee_bringup.h"
#include "xbee_provision.h"

#define XBEE_APP_PROVISION  1
#define XBEE_APP_BRINGUP    2
#ifndef XBEE_APP
#define XBEE_APP  XBEE_APP_PROVISION
#endif
```

`app_init()` and `app_process_action()` call the selected module. `XBEE_APP` is set in the
`# Add additional macros here` section of `cmake_gcc/CMakeLists.txt` as
`XBEE_APP=XBEE_APP_PROVISION`, so switching apps is one edit in an allowed section. Note that
`app.c` is compiled in the generated `slc` object library; the `target_include_directories(slc
PUBLIC ...)` block already exposes `../src/app/inc`. The `slc` target does not see
`target_compile_definitions(xbee_provision ...)`, so the define in `app.c` defaults to provisioning
and the CMake macro documents the switch for the `xbee_provision` target; the plan verifies during
implementation whether `slc` needs the define too and, if so, adds it with
`target_compile_definitions(slc PUBLIC ...)` next to the existing `slc` include block.

### Build wiring (`cmake_gcc/CMakeLists.txt`, allowed sections only)

Sources: `../src/app/src/xbee_provision.c`, `../src/app/src/xbee_provision_table.c`.
Include paths: unchanged (all four layers already listed).
Macros: `XBEE_APP=XBEE_APP_PROVISION`.

### Host tests (`test/`)

- `test/test_xbee_at_table.c`: new cases for `is_provisionable` (count is 108, each excluded
  class rejected), `is_default` (integer, string, bytes, the `IA` 64-bit default), and
  `values_equal` (short versus padded integers, strings, bytes).
- `test/test_xbee_provision_table.c` (new, links `xbee_provision_table.c`, `xbee_at_table.c`,
  `byte_util.c`): coverage equals the provisionable set exactly, widths match, every value
  validates, and with the shipped header every entry `is_default` (so a pristine header writes
  nothing). The compile-time `BD`/`NB`/`SB` checks need the Studio macros; on the host the test
  build defines them to the same values (`-DSL_UARTDRV_EUSART_XBEE_BAUDRATE=9600` and the two
  enumerators as 0) in `test/CMakeLists.txt`, with a comment saying they mirror the generated
  config.

The state machine in `xbee_provision.c` depends on the facade and `app_log`, so it is verified on
target, not on the host.

## Simplicity Studio changes required (PRIME RULE)

| Tool | Component / instance | Setting | Current value | Required value |
| --- | --- | --- | --- | --- |
| none | | | | |

No change. The header must agree with the existing `XBEE` instance (9600 8N1), which the
compile-time checks enforce.

## Resource impact

| Item | RAM | Flash |
| --- | --- | --- |
| `xbee_provision_table` (108 value arrays, about 400 bytes, plus 108 entries of 8 bytes) | 0 | about 1.3 kB |
| `xbee_at_table` default widened to 64 bits | 0 | about 0.6 kB |
| `xbee_provision` state, mismatch record, one `xbee_at_req_t` | about 0.5 kB | about 3 kB with log strings |
| Command mode line buffer 128 to 160 | 32 B (in the request struct, also inside `xbee_at_req_t`) | 0 |

No new peripheral, interrupt or energy-mode consequence; the device stays in EM0 as before.

## Risks and open questions

- **`R1` acceptance is unverified on this firmware.** The manual documents it (7131 to 7140),
  but if the module answers `ERROR` the run falls back to `RE` and logs it. If custom defaults
  were ever set with `%F`, `RE` restores those, not the factory values.
- **`RE`/`R1` behaviour on `CC`, `GT`, `CT` inside the session.** If the module applied the
  restored Command mode parameters at once, the open session is unaffected (they govern entry
  and idle timeout, and the provisioner never idles for 10 s). The facade re-reads them at the
  post-reset bring-up.
- **Reads that legitimately differ from what was written.** `IT` is reduced by the module when
  the samples would exceed the payload (manual 6700 to 6710), and a non-standard `BD` is rounded
  to the nearest achievable rate (6064 to 6067). Such a header value fails verification by
  design; the header comment for each says so.
- **Variant-specific commands.** The SPI pin commands `P5` to `P9` and the 20-bit `PR`/`PD` apply
  to surface-mount parts. On a variant that answers `ERROR` to a read, the audit skips the
  command; a write of a non-default value to it fails the run, which is the correct outcome for a
  header that asks for something the hardware cannot do.
- **`KY` cannot be verified.** Accepted by the user: the readable set is trusted.
- **Nothing in the stack has yet run on hardware.** The guard-time fix from
  `docs/history/2026-09-19-cmd-mode-guard-time-fix.md` is still pending the user's re-test, and
  provisioning depends on Command mode entry working. Phase 1 below gates on it.
- The `XBEE_APP` define and the `slc` target: see "app.c selection"; resolved during
  implementation with a build check, not a guess.

## Phases and verification

Each phase: build (`cd cmake_gcc && cmake --workflow --preset project`, zero warnings), host
tests (`cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build`),
no Studio-owned file in `git status`, history entry
`docs/history/2026-09-19-xbee-provision-app-phase<N>.md`, readme module list updated, plan
status kept current.

1. **Table and helpers**: widen `default_value`, correct `IA`, add the three
   `xbee_at_table` functions, raise `XBEE_CMD_MODE_LINE_MAX`, extend the table tests.
   Verification: host tests; target build; size delta recorded.
2. **Facade session**: `xbee_cmd_session_open/status/close` and the routing flag.
   Verification: host test in `test_xbee_cmd_mode.c` is not applicable (the facade is not
   host-built); static review of the three touched routines; target build. On target, with the
   module in API mode 1: open a session, read `VR`, close, confirm API frames still work after.
3. **Configuration header and compiled table**: the 108 macros, the X-macro list, the value
   arrays, the compile-time link checks, `test_xbee_provision_table.c`.
   Verification: host tests prove coverage, widths and that the pristine header is all-default.
4. **Provisioning app and selection**: `xbee_provision.c/.h`, `app.c` switch, CMake macro.
   Verification on target, in this order:
   - pristine header against a factory module: log ends with "already provisioned", no `WR` sent
     (UART TX byte count shows no `ATWR`);
   - header with `NI`, `ID`, `CH`, `AP=1` and a non-zero `KY` changed: log shows exactly those
     five writes, one `WR`, reset, detection of API mode 1, verification pass;
   - run again unchanged: "already provisioned" in API mode 1 (proves the forced session from
     API mode and the check-first path);
   - set `XBEE_PROV_RESTORE` to `RE`, change `NI` back to default: run writes the remaining
     deviations only and verification shows `NI` restored to a space;
   - a deliberately out-of-range value in the header fails the host test, and a value the
     module rejects (for example `P5=3` on a through-hole variant, if one is available) produces
     `FAIL_WRITE` with the command named;
   - `xbee_uart` stats show zero overruns after a full run.
