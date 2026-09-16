# Log level usage example

Status: Done (ran on target; example later removed, see docs/history/2026-09-17-log-level-demo-removal.md)
Date: 2026-09-17
Related history: docs/history/2026-09-17-log-level-demo.md

## Goal
Add a small example that shows how to use each log level of the `app_log` module
(`docs/plan/2026-09-17-multi-level-log.md`). It emits one line per level (VERBOSE, INFO, WARNING,
ERROR) once at boot.

## Background and references
- `src/utils/inc/app_log.h`: `APP_LOG_TAG`, `APP_LOG_VERBOSE/INFO/WARNING/ERROR`,
  `app_log_get_level()`.
- `src/utils/inc/app_log_config.h`: `APP_LOG_LEVEL_MIN` defaults to INFO.
- `app.c` is built in the generated `slc` OBJECT library, so headers it includes from `src/` need
  `target_include_directories(slc PUBLIC ...)` (`CLAUDE.md`, build registration rule).

## Design
User decisions: `src/app/log_demo` module, run once at boot, keep the INFO default floor.

- `src/app/inc/log_demo.h`: `void log_demo_run(void);`
- `src/app/src/log_demo.c`: `#define APP_LOG_TAG "log_demo"` before `#include "app_log.h"`.
  `log_demo_run()` emits:
  - `APP_LOG_VERBOSE`: detailed trace with a value. Compiled out at the INFO default, which shows
    the compile-time filter. Set `APP_LOG_LEVEL_MIN` to `APP_LOG_LEVEL_VERBOSE` to see it.
  - `APP_LOG_INFO`: normal progress, including the compile-time and runtime levels
    (`APP_LOG_LEVEL_MIN`, `app_log_get_level()`).
  - `APP_LOG_WARNING`: recoverable condition, for example a retry count.
  - `APP_LOG_ERROR`: failure with an `sl_status_t` code.
  The values are named constants. No hardware is touched.
- `app.c`: `#include "log_demo.h"`, and call `log_demo_run()` from `app_init()`.
- Dependency direction: `app.c` -> `app` layer (`log_demo`) -> `utils` (`app_log`). This follows
  `CLAUDE.md`.

### Build registration (`cmake_gcc/CMakeLists.txt`)
- `# Add additional sources here`: `../src/app/src/log_demo.c`
- `# Add additional include paths here`: `../src/app/inc`
- `target_include_directories(slc PUBLIC ../src/app/inc)`, because `app.c` includes
  `log_demo.h`. `log_demo.h` does not include `app_log.h`, so `../src/utils/inc` is not needed on
  `slc` until `app.c` logs itself.
- Remove `src/app/inc/.gitkeep` and `src/app/src/.gitkeep`.

## Simplicity Studio changes required (PRIME RULE)
None.

## Resource impact
- Flash: the newlib-nano printf family (about 4.6 kB) is now linked, because the logger is used
  for the first time. Add the `app_log` object (about 266 B) and the demo strings (under 300 B).
- RAM: negligible. Energy: unchanged (EM0).

## Risks and open questions
- Output is blocking: about 4 lines at 115200 baud, roughly 12 ms once at boot. That is
  acceptable.

## Verification
- Build: exit 0, no warnings. Record `arm-none-eabi-size` against 24164 / 372 / 261772.
- Image check: the INFO, WARNING and ERROR demo strings are present in `xbee_provision.out`, and
  the VERBOSE string is absent.
- On target (user, VCOM 115200 8N1): three lines after reset (`I`, `W` in yellow, `E` in red), with
  tag `log_demo`.
