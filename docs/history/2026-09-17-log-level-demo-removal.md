# Log level example: on-target result and removal

Date: 2026-09-17
Related plan: docs/plan/2026-09-17-log-level-demo.md
Follows: docs/history/2026-09-17-log-level-demo.md

## Summary
The `log_demo` example ran on target. The user asked for it to be removed, so the module, its
call in `app.c` and its CMake entries were deleted. The `app_log` module stays, but nothing uses
it for now.

## Motivation
User request: "remove log_demo".

## Changes
| File | Change |
| --- | --- |
| `src/app/inc/log_demo.h`, `src/app/src/log_demo.c` | Deleted. |
| `src/app/inc/.gitkeep`, `src/app/src/.gitkeep` | Restored (folders are empty again). |
| `app.c` | Restored to the project-init content (empty `app_init()` / `app_process_action()`). |
| `cmake_gcc/CMakeLists.txt` | Removed `../src/app/src/log_demo.c`, `../src/app/inc` and the `target_include_directories(slc PUBLIC ../src/app/inc)` block. `app_log.c` and `../src/utils/inc` remain registered. |
| `docs/plan/2026-09-17-log-level-demo.md` | Status updated. |
| `docs/history/2026-09-17-log-level-demo-removal.md` | This entry. |

## Simplicity Studio changes (made by the user)
None.

## Verification
- On target, before removal (VCOM output pasted by the user): three lines were received, INFO
  `log demo start, compile-time min level 1, runtime level 1`, WARNING `no response, retry 1/3`
  and ERROR `operation failed, status 0x0007`, with timestamps 0.000, 0.006 and 0.011 s. No
  VERBOSE line appeared, which matches the INFO default. The tag in that output was `IoTech-WTX`,
  because the user had changed `APP_LOG_TAG` in `log_demo.c` from `log_demo`. Whether colours
  rendered cannot be seen from the pasted text.
- This on-target run also exercises the `app_log` line format, level filtering and timestamps
  (`docs/plan/2026-09-17-multi-level-log.md`). `app_log_set_level()` was not exercised.
- Removal: build only. `cmake --workflow --preset project` exit 0, no warnings.
  `arm-none-eabi-size` text 24164 / data 372 / bss 261772. The logger is unreferenced again and
  removed by `--gc-sections`.

## Known limitations and follow-ups
- Unreadable characters were seen on VCOM before the first line after pressing reset. Likely
  cause (not confirmed): the TX line (PD08) is undriven while the MCU is in reset and before the
  iostream sets it push-pull high, so the receiver decodes noise. Suggested checks are a raw hex
  capture, a build with `APP_LOG_COLOR_ENABLE 0`, and a scope on TX during reset. A suggested
  hardware fix is a pull-up on TX (for example 10 kΩ to VDD). Nothing has been changed for this.
