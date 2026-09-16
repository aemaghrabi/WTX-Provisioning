# Multi-level log system

Date: 2026-09-17
Related plan: docs/plan/2026-09-17-multi-level-log.md

## Summary
Added a multi-level logger in `src/utils`. It has levels VERBOSE, INFO, WARNING, ERROR, plus NONE,
a compile-time minimum level `APP_LOG_LEVEL_MIN` (default INFO, overridable by define) and a
runtime level that can only raise that floor. Each line carries an uptime timestamp, level letter
and module tag, with optional ANSI colours. Nothing uses it yet.

## Motivation
The user asked for a proper multi-level log system to replace the simple printf demo, with a
minimum log level definable in firmware by `#define`.

## Changes
| File | Change |
| --- | --- |
| `src/utils/inc/app_log.h` | New module header: level defines, `app_log_level_t`, `APP_LOG_VERBOSE/INFO/WARNING/ERROR` macros (compiled out below the floor), `app_log_write()`, `app_log_set_level()`, `app_log_get_level()`. `#error` on an invalid `APP_LOG_LEVEL_MIN`. |
| `src/utils/inc/app_log_config.h` | New. `APP_LOG_LEVEL_MIN` (default `APP_LOG_LEVEL_INFO`) and `APP_LOG_COLOR_ENABLE` (default 1), both `#ifndef`-guarded. |
| `src/utils/src/app_log.c` | New implementation. Filters by the runtime level. Drops calls from IRQ context (`SL_STATUS_INVALID_STATE`). Timestamp from the sleeptimer 64-bit tick, printed as `sec.msec`. Colour and letter lookup tables. |
| `cmake_gcc/CMakeLists.txt` | Registered `../src/utils/src/app_log.c` and `../src/utils/inc` in the `# Add additional` sections. |
| `docs/plan/2026-09-17-multi-level-log.md` | New plan, Status Done (on-target check pending). |
| `docs/history/2026-09-17-multi-level-log.md` | This entry. |

Line format: `[     1.234] I tag: message`

## Simplicity Studio changes (made by the user)
None.

## Verification
Build and compile-level only. Not run on target, because no code calls the logger yet.
- `cmake --workflow --preset project`: exit 0. `app_log.c` compiled with `-Wall -Wextra`, no
  warnings. Image size unchanged (text 24164 / data 372 / bss 261772), because the module is
  unreferenced and removed by `--gc-sections`.
- Scratch test file (outside the repo) calling every macro, compiled with the exact project flags
  from `build/compile_commands.json`. `app_log.c` was compiled the same way. Both were built for
  `APP_LOG_LEVEL_MIN` 0..4 with `APP_LOG_COLOR_ENABLE` 1 and 0, 20 compiles in all. Results:
  - no warnings, including a variable used only in log calls, and log macros in an unbraced
    `if`/`else`;
  - format strings kept in the object: level 0 all four, level 1 INFO/WARNING/ERROR, level 2
    WARNING/ERROR, level 3 ERROR, level 4 none;
  - `APP_LOG_LEVEL_MIN=5` fails the build with the `#error`.
- `app_log.o` text: 266 B with colour, 248 B without. The first real use also pulls in the
  newlib-nano printf family (about 4.6 kB, measured for the earlier demo).

## Known limitations and follow-ups
- On-target check pending until a module logs: line format, colours, timestamps and
  `app_log_set_level()`.
- If `app.c` logs later, it needs `target_include_directories(slc PUBLIC ../src/utils/inc)` (see
  `CLAUDE.md`). A per-build `APP_LOG_LEVEL_MIN` override define probably also needs to reach
  `slc`. This is not verified.
- The logger blocks during the console write (about 3 ms per typical line at 115200 baud) and is
  unsuitable for time-critical paths.
- The logger uses SDK sleeptimer and `sl_core`, so the `utils` layer is no longer purely
  hardware-independent. The user can decide whether the layer description in `CLAUDE.md`
  should change.
