# Boot-time dump of every XBee parameter the module will report

Status: Done, pending on-target verification
Date: 2026-09-19
Related history: docs/history/2026-09-19-xbee-parameter-dump.md,
docs/history/2026-09-19-provision-credential-masking.md

## Goal

Read, at boot and in every build, every AT parameter the XBee module is willing
to report, and log each one on its own line. The dump runs in both applications,
`XBEE_APP_PROVISION` and `XBEE_APP_BRINGUP`, and lives in a module of its own so
that a later build preset can compile the provisioning application without
`src/app/src/xbee_bringup.c` and still get the dump.

## Context

Today the firmware logs almost nothing about what the module actually holds.

- `xbee_bringup` reports the nine parameters the facade reads during bring-up
  (`xbee_info_t`), and nothing else.
- `xbee_provision`'s audit pass reads all 108 provisionable parameters but logs
  only those that differ from the configured value. A module that already
  matches produces no per-parameter output at all, so a boot log cannot answer
  "what is on this module".

A field diagnostic, a returned board or an unexpected radio behaviour all start
with the same question, and the log cannot answer it. Reading all 127 readable
parameters costs roughly 1.3 s at 9600 baud inside a Command mode session that
is already open, which is cheap against a provisioning run that erases and
rewrites flash.

## Background and references

All manual references are to `docs/manuals/xbee_90002273_ref_manual.md` (Digi
XBee 3 802.15.4 RF Module User Guide, 90002273 R).

| Topic | Lines | What it establishes |
| --- | --- | --- |
| Command mode entry | 3030 to 3124 | Command mode is available on the UART in every operating mode, is entered with `+++` and answered with `OK`, and is left with `CN` or by timeout |
| Guard times | 3044 to 3051 | `+++` must be preceded and followed by `GT` of silence, one second by default. This is the fixed entry cost, about 2.1 s, that the session rule below exists to avoid paying twice |
| Command mode timeout (CT) | 3062 to 3065 | With no input for `CT`, ten seconds by default, the module drops out of Command mode. Any accepted command restarts that window |
| AT command chapter | 4812 to 7140 | The 147 commands the table in `src/services/inc/xbee_at_table.h` describes, with their types, access restrictions and defaults |
| KY is not readable | 5449 to 5458 | "This command is write-only and cannot be read. If you attempt to read KY, the device returns an OK status." |

Behaviour of the existing code, verified by reading it in this session, not
assumed:

- `src/services/inc/xbee.h:36-38`: the facade permits exactly one
  `xbee_at_req_t` in flight and returns `SL_STATUS_BUSY` for a second. The one
  request slot is a resource that has to be arbitrated.
- `src/services/src/xbee_cmd_mode.c`, `handle_line()` and
  `refresh_session_deadline()`: after an accepted reply line the transport
  returns to `XBEE_CMD_STATE_ACTIVE`, not `IDLE`, and the session deadline is
  pushed out to CT minus `XBEE_CMD_MODE_CT_MARGIN_MS`, 9.5 s. Back-to-back reads
  therefore pay the Command mode entry cost once, not once per read.
- `src/services/src/xbee.c:163-166`, `use_cmd_transport()`: a forced session
  routes every request through Command mode regardless of the detected mode.
  Forcing a session in an API mode would therefore make each read slower, not
  faster.
- `src/services/src/xbee.c:449`: a request that goes unanswered completes with
  `SL_STATUS_TIMEOUT`. Any other non-`SL_STATUS_OK` result is the module
  answering with a refusal.

## Design

### Layering

New module in the application layer, `src/app/`:

| File | Contents |
| --- | --- |
| `src/app/inc/xbee_dump.h` | Public API |
| `src/app/inc/xbee_dump_config.h` | `#ifndef`-guarded knobs |
| `src/app/src/xbee_dump.c` | Implementation |

Application layer rather than services, because the module arbitrates two
application-level resources: the facade's single request slot, and ownership of
the Command mode session. A services-layer module could not know whether the
application already holds either.

### Public API

```c
sl_status_t xbee_dump_start(void);      // returns at once
void        xbee_dump_process(void);    // tick
bool        xbee_dump_is_finished(void);
sl_status_t xbee_dump_get_result(void);
bool        xbee_dump_is_secret(uint16_t command);
```

The header states that the caller already drives `xbee_process()` and that this
module must not call it. Both applications drive it once per super-loop
iteration; a second call per iteration would drain the receive path twice and
change the timing of every transport.

### Command mode session ownership

Decided once, in `xbee_dump_start()`:

1. `xbee_cmd_session_is_open()` is true: adopt the session and do not close it.
   This is the provisioning path, where `PROV_OPEN` has just opened one.
2. Transparent mode with no session: open one, dump, close it.
3. Any API mode with no session: open nothing. Forcing one would add the ~2.1 s
   entry cost and route every read through Command mode, which is slower than
   the API frames that mode already gives.

### Which entries are dumped

`dump_wanted(entry)` excludes, in this order:

| Exclusion | Why | Count |
| --- | --- | --- |
| `xbee_at_table_validate_get(id) != SL_STATUS_OK` | Write-only (`KY`) and the commands that only execute | 14 |
| `XBEE_AT_FLAG_MULTI_RESPONSE` | `ND`, `ED`, `FS` and `VL` answer with a stream ended by a timeout, not one value | 4 |
| `XBEE_AT_TYPE_SUBCOMMAND` | `PY` takes a text subcommand, not a stored value | 1 |
| `XBEE_AT_DN` | A discovery, not a stored value | 1 |

Measured on the table as it stands: **127 dumped, 20 skipped, of 147**.

`KY` is skipped by the predicate but still gets a line of its own, so the dump
is honest that a key exists without printing it.

### Per-parameter outcomes

| Outcome | Line | Level |
| --- | --- | --- |
| Read succeeded | `CH (Operating Channel) = 0C` | INFO |
| Module refused | `BT (Bluetooth Enable) = (refused)` | INFO |
| Secret, not readable | `KY (AES Encryption Key) = (write only, 16 bytes)` | INFO |
| Secret, readable | `*S (Secure Session Salt) = (4 bytes, not logged)` | INFO |
| `SL_STATUS_TIMEOUT` | `FK (File System Public Key) = (no answer)` | WARNING |
| `xbee_at_get()` returned non-OK | one line, dump abandoned | ERROR |

A refusal is INFO, not WARNING. It is the expected answer for the commands this
802.15.4 variant does not implement (`P5` to `P9`, the Bluetooth set, the secure
session set, `FK`), and it costs one ~10 ms round trip rather than a timeout.

Values are printed bare, with no `(default ...)` annotation.

### Secret masking

`xbee_dump_is_secret(command)` is true for `KY`, the Secure Session values
`*S *V *W *X *Y`, and the Bluetooth SRP values `$S $V $W $X $Y`. One definition,
used by the dump and by `start_next_write()` in `xbee_provision.c`.

This closes a live leak: `xbee_provision.c` currently prints the AES key in full
hexadecimal to VCOM on every provisioning run that writes it, with a real
16-byte key configured at `xbee_provision_config.h:198`. Masked values log as a
byte count, `(16 bytes, not logged)`.

### State machine

```c
typedef enum { DUMP_IDLE, DUMP_OPEN, DUMP_READ, DUMP_CLOSE, DUMP_DONE } dump_state_t;
```

`xbee_dump_process()` issues at most one `xbee_at_get()` and emits at most one
log line per call. In `DUMP_READ` a tick either completes the read in flight and
logs its one line, or advances the table walk and issues the next read. The walk
itself is pure table inspection with no I/O; an excluded entry costs no tick
unless it is `KY`, whose masked line is one tick of its own.

### Two independent bounds

Both use the `tick_reached()` / `deadline_from_ms()` pattern already in
`src/app/src/xbee_provision.c:166-183`.

| Knob | Default | Why |
| --- | --- | --- |
| `XBEE_DUMP_MAX_CONSECUTIVE_TIMEOUTS` | 3 | Three unanswered reads in a row mean the link is gone. Continuing would spend a per-request timeout on each of the remaining entries |
| `XBEE_DUMP_TOTAL_TIMEOUT_MS` | 30000 | Absolute deadline, checked at the top of every tick, so no combination of slow answers can run unbounded |
| `XBEE_DUMP_OPEN_TIMEOUT_MS` | 10000 | Bounds case 2 above |
| `XBEE_DUMP_CLOSE_TIMEOUT_MS` | 15000 | Bounds the close; the module's own CT is 10 s |
| `XBEE_DUMP_VALUE_MAX_BYTES` | 16 | Bytes of a value printed before it is abbreviated |

An abandoned dump is a diagnostic failure only. It never becomes a provisioning
failure.

### Shared helper promotion

`value_text()` in `xbee_provision.c` is private, and the dump needs the same
formatting. It moves to `src/utils/`, next to the already host-tested
`byte_util_hex_encode()`:

```c
const char *byte_util_hex_text(char *out, uint16_t cap,
                               const uint8_t *in, uint16_t len,
                               uint16_t max_bytes);
```

The caller provides the buffer so two values can be printed side by side in one
call, which the audit's mismatch line does. Returns `"(empty)"` and
`"(unprintable)"` for the degenerate cases and appends an ellipsis past
`max_bytes`, matching today's behaviour exactly. The private `value_text()` is
deleted.

`command_name()` in `xbee_provision.c` currently returns a shared
`static char[4]`, so two command names in one `printf` would collide. It takes
the same caller-buffer shape.

### Log lines removed

The dump prints every readable value, so a line that does nothing but restate
one of those values becomes noise. Removed:

- `xbee_bringup.c`, `report_success()`: the serial number line (SH, SL), the
  firmware/hardware line (VR, HV) and the max-payload/output-options line
  (NP, AO).
- `xbee_provision.c`, `report_module()`: the serial and firmware fields, leaving
  a mode-only line.
- `src/services/src/xbee.c:626`: the `AP=%u` field, leaving
  `"detected %s mode"`.

Kept, because none of these is a parameter restatement: the
`"module ready in <mode> mode"` phase markers, the AO=2 legacy-frames WARNING (a
judgement about a value, not a listing of it), the UART link statistics and the
overrun warning (driver counters, not AT parameters), the audit's
`"%s is %s, configured as %s"` mismatch line (a comparison against the
configured value, which the dump does not make), and every WARNING and ERROR
line throughout.

### Integration

`xbee_bringup.c` gains a `BRINGUP_DUMPING` state, entered after
`report_success()`. The `if (state != BRINGUP_WAITING) return;` guard moves
below the new branch, or the dump would never be ticked.

`xbee_provision.c` gains `PROV_DUMP` between `PROV_OPEN` and `PROV_AUDIT`, so in
Transparent mode the dump reuses the session `PROV_OPEN` has just opened and
still reflects the as-found configuration, the restore happening only after the
audit. It is guarded with `!verifying`: the verification pass skips it, because
the module has just been written and the audit that follows checks every value.
The whole thing is gated on `XBEE_PROV_DUMP`, default 1, in
`src/app/inc/xbee_provision_config.h`.

The provisioning `req` is untouched throughout `PROV_DUMP`. The dump owns its
own request and `req_active` stays false, so `process_request()` is never
entered.

### Build registration

`cmake_gcc/CMakeLists.txt`, in the allowed sections only:

- `# Add additional sources here`: `../src/app/src/xbee_dump.c`
- `# Add additional macros here` **and** duplicated onto the `slc` target, as
  `XBEE_APP` already is: `XBEE_AT_TABLE_NAMES=1`

The duplication is not cosmetic. `XBEE_AT_TABLE_NAMES` changes the layout of
`xbee_at_entry_t` (`src/services/inc/xbee_at_table.h:369-371` adds a
`const char *name` member under the `#if`). Defining it on one target only would
be a silent struct-size disagreement between `app.c` and everything else.

No new include directory is needed: `../src/app/inc` is already on both targets.

### Sample output

```
[    12.341] I dump: reading every parameter the module will report, 127 of 147 commands
[    12.352] I dump: CH (Operating Channel) = 0C
[    12.365] I dump: ID (Extended PAN ID) = 1234
[    12.510] I dump: KY (AES Encryption Key) = (write only, 16 bytes)
[    12.535] I dump: SA (Secure Access) = (refused)
[    13.902] W dump: FK (File System Public Key) = (no answer)
[    14.612] I dump: dump complete: 121 read, 5 refused, 1 unanswered, 20 not readable
```

## Simplicity Studio changes required (PRIME RULE)

**None.**

The dump uses only existing application and service code: the facade
(`xbee_at_get()`, the Command mode session calls), the AT command table, the
sleeptimer for its deadlines and `app_log` over the existing VCOM iostream
instance. No new peripheral, pin, clock or software component is involved, and
no file under `config/`, `autogen/` or the generated `cmake_gcc/` files is
touched. The only build change is in the `# Add additional ...` sections of
`cmake_gcc/CMakeLists.txt`, which CLAUDE.md allows.

## Resource impact

- **Flash:** the module itself, plus roughly 2 kB for the command-name strings
  that `XBEE_AT_TABLE_NAMES=1` compiles in. Measured delta is recorded in the
  history entry.
- **RAM:** one `xbee_at_req_t` (about 150 bytes) plus a handful of counters and
  deadlines, all statically allocated. No dynamic allocation.
- **Peripherals:** none beyond those already in use, EUSART0 for the module and
  EUSART2 for the log.
- **Energy:** the dump extends the boot sequence by roughly 1.3 s of EM0/EM1
  activity in an already-open session, or about 3.4 s in Transparent mode where
  it opens and closes a session of its own. It adds no periodic work: once
  `xbee_dump_is_finished()` is true the module does nothing.

## Risks

| Risk | Mitigation |
| --- | --- |
| A module that stops answering leaves the dump spinning | Two independent bounds, consecutive timeouts and an absolute deadline |
| The dump delays provisioning, or fails it | The dump runs before the audit and never sets a provisioning result; an abandoned dump falls through to `start_read_pass()` either way |
| Log volume: 129 extra lines per boot at 115200 baud | Roughly 5 kB, about 0.4 s of VCOM time. Accepted; the dump is the point of the change |
| A credential printed in the clear | `xbee_dump_is_secret()`, shared by the dump and the provisioning write path |
| `XBEE_AT_TABLE_NAMES` defined on one target only | Defined on both `xbee_provision` and `slc`, and the reason is written next to it in `CMakeLists.txt` |

## Test and verification approach

1. Host tests, including new `byte_util_hex_text()` cases in
   `test/test_byte_util.c`.
2. Target build with zero warnings.
3. Text-segment size delta from `cmake_gcc/build/base/xbee_provision.map`.
4. Static review that `xbee_dump_process()` issues at most one `xbee_at_get()`
   and emits at most one log line per call, and that both bounds are reachable.
5. A grep of the boot path confirming no parameter is printed twice.

**No hardware is available for this work.** Verification is build, host tests
and static review only.
