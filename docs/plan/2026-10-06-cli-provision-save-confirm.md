# Console `xbee provision all`: confirm before saving to flash

Status: Done, pending on-target verification
Date: 2026-10-06
Related plan: docs/plan/2026-10-06-cli-provision-all.md
Related history: docs/history/2026-10-06-cli-provision-save-confirm.md

Implemented as designed, with the differences listed under "Implementation notes". The CLI
build compiles and links with no warnings. Not yet tested on target.

## Context

Today `xbee provision all` (CLI build) runs audit -> restore (R1/RE) -> write deviating params ->
`WR` -> close -> reset -> verify, with only an up-front `[confirm]`. The operator never sees what
the module will save before flash is written (10 000 cycle endurance, manual lines 7089-7095).
New behaviour: after the writes, read back every provisioning parameter from the module, print it
as `{Parameter}={value}`, then ask `do you want to save? [N/y]`; `WR` is sent only on an explicit
`y`.

Decisions taken with the user:
- **Scope:** CLI only. The standalone `XBEE_APP_PROVISION` boot build keeps committing
  automatically (no operator).
- **Decline** (N, Enter, other text, Ctrl-C, timeout): skip `WR`, `xbee_hw_reset()` so the module
  comes back on its unchanged flash config. Never send `CN` (it would apply the staged AP/BD).
- **List:** every provisioning-table entry, table order, formatted like `show xbee at`
  (`xbee_dump_format_value()`), e.g. `CH=0C`, `NI=SENSOR1`, `KY=(not readable)`.
- **Mismatch on read-back:** list it (marked), and the single prompt flips its default to yes:
  `<n> parameters did not read back as written. Save anyway? [Y/n]` (Enter = save). With no
  mismatch the prompt is `do you want to save? [N/y]` and needs an explicit `y`/`yes`.
- **Verify after a save with mismatches:** those parameters are verified against the value that
  was read back and saved, and the run ends with a new result "written with N parameters
  different from the configuration", which the CLI reports as a failure (`% Provisioning ...`).

## Design

### Engine: [xbee_provision.c](../../src/app/src/xbee_provision.c), [xbee_provision.h](../../src/app/inc/xbee_provision.h)

Additive; standalone path unchanged (no review callback -> PROV_WRITE -> start_commit() as today).

- `xbee_provision_start(xbee_prov_review_cb_t review_cb)` (only caller is `cli.c`); NULL means
  no confirmation. `prepare_run()` resets the new state below.
- New states:
  - `PROV_REVIEW` -- read-back pass after the last write (and after "no parameter deviates").
    Reuses `start_next_read()`/`table_index`; handled by a new `handle_review_result()` rather
    than overloading `handle_read_result()`. Reads every table entry, including non-readable ones
    which are reported without a request.
  - `PROV_AWAIT_DECISION` -- no request in flight; waits for `xbee_provision_decide()`.
    Keep-alive: the session times out after the module's active CT (10 s default, manual lines
    7071-7078: timeout applies the changes). Every half of the session window (the CT from
    `xbee_get_info()` minus `XBEE_CMD_MODE_CT_MARGIN_MS`) issue a harmless `xbee_at_get(XBEE_AT_CT)`; any
    accepted command pushes the transport's session deadline (`xbee_cmd_mode.c:110`). If the
    session is lost anyway (`!xbee_cmd_session_is_open()`), the run fails with
    `XBEE_PROV_RESULT_FAIL_SESSION`, logging that the module may now run unsaved values.
    A decision that arrives while a keep-alive is in flight is latched and acted on when it
    completes.
  - `PROV_DISCARD` -- after `xbee_hw_reset()` on decline; waits for bring-up like `PROV_BRINGUP`
    but finishes with `XBEE_PROV_RESULT_DECLINED` (or `FAIL_RESET` if bring-up fails).
- Review reporting to the host through a callback set at start (keeps exact `{Parameter}={value}`
  output free of log prefixes):
  `typedef void (*xbee_prov_review_cb_t)(uint16_t command, xbee_prov_review_t kind, const uint8_t *value, uint16_t len, const uint8_t *configured, uint16_t configured_len);`
  with `kind` = `MATCH`, `MISMATCH`, `UNREADABLE` (KY), `READ_FAILED` (variant lacks it; value
  NULL, status logged).
- Accepted read-back values for verify: fixed pool `accepted[XBEE_PROV_MAX_ACCEPTED]` (default 8)
  of `{uint16_t index; uint16_t len; uint8_t value[XBEE_AT_VALUE_MAX];}` (~550 B RAM). More
  mismatches than slots -> no prompt offered, run fails `XBEE_PROV_RESULT_FAIL_REVIEW` after a
  discard reset (cannot verify what it would save). `handle_read_result()` in verify mode compares
  against the accepted value when the index is in the pool.
- New API: `bool xbee_provision_awaiting_decision(void)`, `uint16_t xbee_provision_review_mismatches(void)`,
  `sl_status_t xbee_provision_decide(bool save)` (INVALID_STATE outside the decision point).
- New results + `xbee_provision_result_str()` text: `XBEE_PROV_RESULT_DECLINED` ("not saved, the
  module was reset to its stored configuration"), `XBEE_PROV_RESULT_WRITTEN_WITH_DIFFERENCES`,
  `XBEE_PROV_RESULT_FAIL_REVIEW`. `xbee_provision_passed()` unchanged (DECLINED is not a pass, the
  CLI reports it as `% Not saved: ...` rather than a failure).
- `xbee_provision_abort()` additionally accepted in `PROV_AWAIT_DECISION` (= decide(false)).
- Header doc block updated to describe the hosted review step.

### Console: [cli.c](../../src/app/src/cli.c), [cli_config.h](../../src/app/inc/cli_config.h)

- New state `CLI_STATE_SAVE_CONFIRM`; not in `is_busy()` so the line editor collects the answer.
- `review_cb` prints with `console_uart_printf`: `CH=0C`, mismatches as
  `CH=0B (differs, configured 0C)`, `KY=(not readable)`, `D5=(read failed)`. Uses
  `command_text()` and `xbee_dump_format_value()`.
- `CLI_STATE_PROVISION`: when `xbee_provision_awaiting_decision()` -> print the prompt chosen by
  mismatch count, set `save_confirm_deadline = CLI_SAVE_CONFIRM_TIMEOUT_MS` (new, default 60000),
  state `CLI_STATE_SAVE_CONFIRM`. `xbee_provision_process()` keeps being called in this state (it
  drives the keep-alive).
- `handle_save_confirm()`: no mismatch -> save only on `y`/`yes` (case-insensitive, reuse
  `equals_ignore_case()`); with mismatches -> save on Enter/`y`/`yes`, decline on anything else.
  Ctrl-C (LINE_EDIT_ABORT), overflow and timeout -> decline. Then back to `CLI_STATE_PROVISION`.
- `finish_provision()`: DECLINED -> `% Not saved: ...`; WRITTEN_WITH_DIFFERENCES -> `% Provisioning
  ...` failure line; `config_dirty` cleared only on pass (unchanged) and after DECLINED (module was
  reset, earlier unsaved `xbee at` writes are gone too -- same as `reload`).
- The existing up-front `[confirm]` prompt is left unchanged.

### Documentation (project rules)
- `docs/plan/2026-10-06-cli-provision-save-confirm.md` (this design, `Status:` line).
- `docs/history/2026-10-06-cli-provision-save-confirm.md`.
- No Simplicity Studio changes needed.

## Defaults proposed here (change on approval if wanted)
`CLI_SAVE_CONFIRM_TIMEOUT_MS` 60000, `XBEE_PROV_MAX_ACCEPTED` 8, mismatch marker text
`(differs, configured <value>)`.

## Implementation notes

- Keep-alive: the planned 1000 ms floor and `XBEE_PROV_KEEPALIVE_MS` override were dropped. A
  floor above half of a short CT (CT may be as low as 200 ms) would let the session lapse, so
  the interval is always half of the window the transport allows.
- A read that cannot be started during the read-back, or more mismatches than
  `XBEE_PROV_MAX_ACCEPTED`, ends the run through the same reset as a decline, with
  `XBEE_PROV_RESULT_FAIL_REVIEW`. The session is never closed with `CN` once values are staged
  for review.
- The engine logs a declined run as a warning ("provisioning not saved"), not as a failure.
- `?` at the `[confirm]` and save prompts lists the accepted answers and repeats the question,
  instead of printing the command help (decided by the project owner after implementation).

## Resource impact
~600 B RAM (accepted pool + few statics), ~1.5 KB flash. No new peripherals; EM unchanged.

## Risks (to verify on target)
- Manual does not state whether a query in Command mode returns the staged or the active value
  (lines 3101-3110). If it returns the active value, every changed parameter would show as a
  mismatch; first on-target run will reveal it.
- RE/R1 effect on the session's active CT: keep-alive uses the CT read at bring-up; verify the
  session survives a 60 s wait.
- Note in passing (not changed): the comment at `xbee_provision.c:84` says restore writes flash;
  manual lines 7100-7106 say RE only restores parameters. Proposed as a separate comment fix.

## Verification
1. Build: `cd cmake_gcc && cmake --workflow --preset project`, zero warnings; also build with
   `XBEE_APP=XBEE_APP_PROVISION` to confirm the standalone path still compiles.
2. On target (CLI): `enable` / `configure terminal` / `xbee provision all` / Enter ->
   list of `{Parameter}={value}`, then `do you want to save? [N/y]`.
   - `y` -> `[OK] written, committed and verified`.
   - Enter / `n` / Ctrl-C / 60 s wait -> `% Not saved`, `show xbee all` shows the old config.
   - Wait > 10 s before answering `y` -> still saves (keep-alive works).
3. Standalone build: behaviour identical to today (no prompt, commits).
