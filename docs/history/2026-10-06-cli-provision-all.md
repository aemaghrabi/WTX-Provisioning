# Plan for the console command `xbee provision all`

Date: 2026-10-06
Related plan: docs/plan/2026-10-06-cli-provision-all.md

## Summary

Added the plan for `xbee provision all`, a console command in global configuration mode that
runs the provisioning engine from the CLI build. No code changed.

## Motivation

Requested by the project owner: run the provisioning routine from the console in configure
terminal mode, under the name `xbee provision all`.

## Changes

| File | Change |
| --- | --- |
| `docs/plan/2026-10-06-cli-provision-all.md` | New plan, Status Approved |
| `docs/history/2026-10-06-cli-provision-all.md` | This entry |

## Simplicity Studio changes (made by the user)

| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |

None.

## Verification

Documentation only; nothing to build.

## Known limitations and follow-ups

- Implementation not started. It follows the plan, and gets its own history entry when done.

---

# Console command `xbee provision all`, implementation

Date: 2026-10-06
Related plan: docs/plan/2026-10-06-cli-provision-all.md

## Summary

The console now has `xbee provision all` in global configuration mode. After an IOS-style
`[confirm]`, it runs the provisioning engine against the module the console already drives. The
engine's progress shows at INFO level during the run, and the previous log level comes back
afterwards.

## Motivation

Implements the plan in the entry above.

## Changes

| File | Change |
| --- | --- |
| `src/app/inc/xbee_provision.h` | New `xbee_provision_start()` (hosted start, no `xbee_init()`) and `xbee_provision_abort()`. New result `XBEE_PROV_RESULT_ABORTED`, appended last. File Doxygen describes standalone and hosted use |
| `src/app/src/xbee_provision.c` | Run setup factored into `prepare_run()`. `own_facade` stops `xbee_process()` from being called twice per pass when hosted. `abort_requested` is honoured only before anything is written: in `PROV_BRINGUP`, after the session opens, and between audit reads. Result text for `ABORTED`. Standalone behaviour unchanged |
| `src/app/src/cli.c` | `provision` -> `all` under `xbee` in `M_CONF`. New states `CLI_STATE_CONFIRM` (Enter, `y` or `yes` confirms; anything else, Ctrl-C, an overlong line or a `CLI_PASSWORD_TIMEOUT_MS` timeout cancels) and `CLI_STATE_PROVISION` (busy; Ctrl-C asks the engine to abort, or says once that the run cannot be interrupted). Log level is raised to INFO for the run and restored afterwards. `config_dirty` is cleared on a passing result |
| `docs/plan/2026-10-06-cli-provision-all.md` | Status -> Done, pending on-target verification |
| `docs/history/2026-10-06-cli-provision-all.md` | This entry |

## Simplicity Studio changes (made by the user)

| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |

None.

## Verification

- Build: the CLI variant (default `XBEE_APP`) builds with `cmake --workflow --preset project`, using
  the toolchain under `~/.silabs/slt/installs/conan`, with no warnings under `-Wall -Wextra`.
- Not built: the `XBEE_APP_PROVISION` and bridge variants, because selecting them means editing
  `XBEE_APP` in `cmake_gcc/CMakeLists.txt`. `xbee_provision.c` is the same object in every variant,
  and only `app.c` differs.
- Not run: the on-target checks listed in the plan.

## Known limitations and follow-ups

- On-target verification is still outstanding.
- Parameters set with `xbee at` and not saved are discarded by the engine's restore. The
  confirmation text warns about this.
