# Log level usage example

Date: 2026-09-17
Related plan: docs/plan/2026-09-17-log-level-demo.md

## Summary
Added the `log_demo` module in the `app` layer. At boot, `app_init()` calls `log_demo_run()`, which
emits one example line per log level. This is the first use of the `app_log` module.

## Motivation
The user asked for a simple example demonstrating the use of each log level.

## Changes
| File | Change |
| --- | --- |
| `src/app/inc/log_demo.h` | New. Declares `log_demo_run()`. |
| `src/app/src/log_demo.c` | New. Tag `log_demo`. Uses VERBOSE (trace value), INFO (compile-time and runtime levels), WARNING (retry count) and ERROR (status code). |
| `src/app/inc/.gitkeep`, `src/app/src/.gitkeep` | Removed. |
| `app.c` | Includes `app.h` and `log_demo.h`. `app_init()` calls `log_demo_run()`. |
| `cmake_gcc/CMakeLists.txt` | Added `../src/app/src/log_demo.c` and `../src/app/inc` in the `# Add additional` sections, and `target_include_directories(slc PUBLIC ../src/app/inc)` for `app.c`. |
| `docs/plan/2026-09-17-log-level-demo.md` | New plan. |
| `docs/history/2026-09-17-log-level-demo.md` | This entry. |

## Simplicity Studio changes (made by the user)
None.

## Verification
Build only.
- `cmake --workflow --preset project`: exit 0. `app.c`, `app_log.c` and `log_demo.c` compiled with
  `-Wall -Wextra`, no warnings.
- `arm-none-eabi-size`: text 24164 -> 28840 B (+4676), data 372 -> 468 B (+96),
  bss 261772 -> 261676 B (-96). Most of the growth is newlib-nano printf, now linked because the
  logger is used for the first time.
- `arm-none-eabi-strings` on `xbee_provision.out`: the INFO, WARNING and ERROR demo format strings
  and the log prefix format are present. The VERBOSE string (`rx buffer holds`) is absent, which
  confirms the compile-time filter at the default `APP_LOG_LEVEL_MIN` (INFO).

## Known limitations and follow-ups
- On-target check pending. On VCOM 115200 8N1, expect three lines after reset:
  `I log_demo: log demo start, compile-time min level 1, runtime level 1`,
  `W log_demo: no response, retry 1/3` (yellow) and
  `E log_demo: operation failed, status 0x0007` (red).
  This check also covers the pending on-target verification of
  `docs/plan/2026-09-17-multi-level-log.md`.
- The ERROR line uses `SL_STATUS_TIMEOUT` (0x0007) purely as an example value.
