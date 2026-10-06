# Console command `xbee provision all`

Status: Done, pending on-target verification
Date: 2026-10-06
Related history: docs/history/2026-10-06-cli-provision-all.md

Implemented as designed. The CLI build (the default variant) compiles and links with no warnings
under `-Wall -Wextra`. Verification steps 2 and 3 have not been run.

## Goal

Add the console command `xbee provision all`. It is available in global configuration mode
(`xbee(config)#`) and runs the provisioning engine that already exists in
`src/app/src/xbee_provision.c` from the CLI build. The engine applies the configuration in
`src/app/inc/xbee_provision_config.h`: audit, restore, write, `WR`, reset, verify.

Measurable outcome: in the CLI build, `enable` -> `configure terminal` -> `xbee provision all` ->
confirm provisions an unprovisioned module and ends with `[OK] written, committed and verified`.
A second run ends with `[OK] already provisioned, nothing written`. Neither run needs a reflash
of the `XBEE_APP_PROVISION` build.

Behaviour decided by the project owner:

- **Output:** for the duration of the run, the log level is raised to INFO, so the engine's
  existing `APP_LOG` lines appear on the console. The previous level is then restored, and a
  one-line result is printed.
- **Confirmation:** an IOS-style `[confirm]` prompt is shown before anything is sent.
- **Ctrl-C:** it is honoured only before anything is written (bring-up, session open, audit).
  After the restore has started, the run cannot be interrupted, and the console says so.

## Background and references

- `src/app/inc/xbee_provision.h`: the engine runs once at boot today. `xbee_provision_init()`
  calls `xbee_init()`, and `xbee_provision_process()` drives `xbee_process()` itself.
- `src/app/src/cli.c`: the console already initialises the facade (`cli_init()`) and drives
  `xbee_process()` on every pass (`cli_process()`). It carries one request at a time. The
  `show xbee all` walk (`src/app/src/cli_show.c`) is the existing pattern for a long-running
  operation that owns the facade while the console waits, including the Ctrl-C handling that
  never abandons a request in flight.
- `src/services/inc/xbee.h`: `xbee_cmd_session_open()`, `xbee_cmd_session_close()` and
  `xbee_hw_reset()`. The engine uses these. The console never opens a forced session, so there
  is no conflict.
- `docs/manuals/xbee_90002273_ref_manual.md`, lines 3101 to 3110: writes made in Command mode are
  staged until the session ends. Lines 7089 to 7099: `WR` and flash endurance. The engine already
  relies on both, and nothing changes here.

## Design

### Engine: `src/app/src/xbee_provision.c`, `src/app/inc/xbee_provision.h`

The engine has been verified on hardware, so every change is additive. The standalone
`XBEE_APP_PROVISION` path keeps its exact behaviour.

- `static bool own_facade`:
  - `xbee_provision_init()` sets it to true and still calls `xbee_init()`.
  - `xbee_provision_process()` calls `xbee_process()` only when it is true. In the console,
    `cli_process()` already drives the facade, and a second call on the same pass would drain
    the receive path twice.
- `static sl_status_t prepare_run(void)` holds the run setup that is currently inline in
  `xbee_provision_init()`: load the table, then reset `result`, `pending_result`, `req_active`,
  `verifying`, `restore_fell_back`, `written_count`, `table_index` and `abort_requested`. Both
  entry points call it.
- `sl_status_t xbee_provision_start(void)` is the new hosted entry point. It does not call
  `xbee_init()`. It returns:
  - `SL_STATUS_INVALID_STATE` while a run is in progress (state neither `PROV_IDLE` nor
    `PROV_DONE`);
  - `SL_STATUS_NOT_READY` if `!xbee_is_ready()`;
  - otherwise it calls `prepare_run()`, sets `own_facade = false` and `state = PROV_BRINGUP`.
    Because the facade is already ready, the next pass goes straight to `report_module()` and
    opens the session.
- `sl_status_t xbee_provision_abort(void)` sets `static bool abort_requested`.
  - It is accepted (`SL_STATUS_OK`) only while `!verifying` and the state is `PROV_BRINGUP`,
    `PROV_OPEN` or `PROV_AUDIT`. Otherwise it returns `SL_STATUS_INVALID_STATE`, which is the
    point of no return.
  - The request is honoured only at safe points, never by abandoning a request in flight:
    - `PROV_BRINGUP`: `finish(XBEE_PROV_RESULT_ABORTED)`.
    - `PROV_OPEN`, once the session reports open: `close_session(XBEE_PROV_RESULT_ABORTED)`.
    - `handle_read_result()`, before starting the next read:
      `close_session(XBEE_PROV_RESULT_ABORTED)`.
- `XBEE_PROV_RESULT_ABORTED` is appended last to `xbee_prov_result_t`. Its text in
  `xbee_provision_result_str()` is "aborted by the operator, nothing written", and
  `xbee_provision_passed()` is false for it.
- The file Doxygen is updated to describe both uses: standalone at boot and hosted by the
  console.

### Console: `src/app/src/cli.c`

- **Grammar.** `provision` is added to `conf_xbee_children` (count 1 -> 2), with the child `all`
  carrying the new `CMD_XBEE_PROVISION_ALL`. Both nodes are `M_CONF`. Help and `?` follow from
  the tree.
- **New states.**
  - `CLI_STATE_CONFIRM` is the `[confirm]` prompt. Its deadline is `CLI_PASSWORD_TIMEOUT_MS`.
  - `CLI_STATE_PROVISION` means the engine owns the facade. `is_busy()` includes it, so the
    console starts no request of its own.
- **Command.**
  - If `xbee_is_ready()` is false, the console prints
    `% The module is not ready. Use "reload" first.` and stays at the prompt.
  - Otherwise it prints
    `Provisioning restores defaults, writes the module's flash and resets it. Proceed? [confirm]`
    and enters `CLI_STATE_CONFIRM`.
- **Confirm.**
  - An empty line, `y` or `yes` (any case) starts the run.
  - Any other line, Ctrl-C, or the timeout prints `% Cancelled` and returns to the prompt.
  - `feed_byte()` routes `LINE_EDIT_READY` and `LINE_EDIT_ABORT` for this state the way it
    already does for `CLI_STATE_PASSWORD`.
- **Start.**
  1. Save `app_log_get_level()`.
  2. If the level is above INFO, set INFO. A VERBOSE level set by the operator is not lowered.
     If the set fails (`SL_STATUS_INVALID_RANGE`, INFO compiled out), the run continues and
     shows only the final result.
  3. Call `xbee_provision_start()`. On error, restore the level, print the status and return to
     the prompt.
- **Run.** On each pass in `CLI_STATE_PROVISION`, call `xbee_provision_process()`. When
  `xbee_provision_is_finished()` returns true:
  1. Restore the log level.
  2. Print `[OK] <result text>` or `% Provisioning failed: <result text>`.
  3. If the result is `WRITTEN` or `ALREADY_PROVISIONED`, clear `config_dirty`, because the
     module's flash now holds the header configuration.
  4. Return to the prompt.
- **Ctrl-C during the run.**
  - If `xbee_provision_abort()` returns OK, print `% Stopping after the current read`.
  - Otherwise print `% Provisioning is writing the module and cannot be interrupted`, once per
    run.
- **Reset during the run.** The engine resets the module and waits for bring-up itself
  (`PROV_RESET` -> `PROV_BRINGUP`). The console stays in `CLI_STATE_PROVISION` throughout, so
  `advance_bringup()` does not run.

### Build

`xbee_provision.c` and `xbee_provision_table.c` are already listed in `cmake_gcc/CMakeLists.txt`
for every variant, so no CMake change is needed. `app.c` is unchanged.

## Simplicity Studio changes required (PRIME RULE)

| Tool | Component / instance | Setting | Current value | Required value |
| --- | --- | --- | --- | --- |

None. The change is application code only.

## Resource impact

- **RAM:** a few bytes of new statics (`own_facade`, `abort_requested`, the saved log level, the
  confirm deadline).
- **Flash:** about 1 KB of strings and state handling. The engine is already linked into the CLI
  build.
- **Peripherals and interrupts:** none new.
- **Energy:** unchanged. The console build already stays in EM0.
- **Module flash:** one `WR` per run that writes, and none when the module already matches.

## Risks and open questions

- Log lines and console output share VCOM. This is acceptable, because the console echoes
  nothing while it is busy.
- Parameters written with `xbee at` and not saved are discarded by the engine's restore. The
  `[confirm]` text says that defaults are restored.
- If the configuration changes `AP`, the module comes back in another serial mode after the
  reset. The facade runs with `XBEE_MODE_AUTO` and re-detects it, and the engine already checks
  the expected mode before verifying. `BD` is locked to the XBEE instance by `#error`.

## Verification

1. Firmware build of the CLI, provisioning and bridge variants (`cmake --workflow --preset
   project` in `cmake_gcc/`) with zero warnings. If the toolchain is not available on this
   machine, report the change as checked by static review only.
2. Standalone regression: the `XBEE_APP_PROVISION` build provisions and verifies as before.
3. On target, CLI build:
   - `xbee provision ?` in `xbee(config)#` lists `all`.
   - `xbee provision all`, then Enter: the INFO log shows audit, restore, write, `WR`, reset and
     verify, then `[OK] written, committed and verified`. A second run gives
     `[OK] already provisioned, nothing written`.
   - `n` at `[confirm]` cancels, and nothing is sent.
   - Ctrl-C during the audit gives `% Provisioning failed: aborted by the operator, nothing
     written`, and the module is unchanged.
   - Ctrl-C after "restoring defaults" prints the cannot-be-interrupted note, and the run
     completes.
   - Afterwards the log level is back to its previous value: no log lines appear on
     `show xbee at CH`.
