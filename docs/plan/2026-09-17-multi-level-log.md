# Multi-level log system

Status: Done (build and compile-level verification; on-target check deferred until a module logs)
Date: 2026-09-17
Related history: docs/history/2026-09-17-multi-level-log.md
Builds on: docs/plan/2026-09-17-vcom-printf.md (commit 84f72a7)

## Goal
Replace the minimal `APP_LOG()` wrapper in `src/utils` with a multi-level log system:
- four levels: VERBOSE, INFO, WARNING, ERROR, plus a filter-only `NONE` that disables all output;
- a minimum level set in firmware by `#define` (compile-time floor; disabled levels cost no flash);
- a runtime level that can raise the floor but never go below it;
- each line carries an uptime timestamp, a level letter and a per-file module tag;
- optional ANSI colour per level;
- the old printf demo is removed first: the banner and heartbeat in `app.c`, plus the old
  `app_log.h`/`app_log.c` and their CMake entries (user decision at implementation start). The
  new module is then written from scratch, and `app.c` does not log for now.

## Background and references
- Previous module (removed by this change): `src/utils/inc/app_log.h`, `src/utils/src/app_log.c` (`vprintf` wrapper over the
  VCOM iostream, via `iostream_retarget_stdio`).
- SDK `platform/service/iostream/src/sl_iostream_retarget_stdio.c` provides `_write` and `_isatty`,
  so newlib-nano stdout is line-buffered. Output leaves in one flush at `\n`.
- SDK `platform/common/inc/sl_core.h:399`: `bool CORE_InIrqContext(void)`.
- SDK `platform/service/sleeptimer/inc/sl_sleeptimer.h:322`: `sl_sleeptimer_get_tick_count64()`.
  At `:741`: `sl_sleeptimer_tick64_to_ms()` (returns `sl_status_t`, rounds down).
- Sleeptimer runs on SYSRTC clocked from LFRCO (`config/sl_sleeptimer_config.h`, commit e439304).
- Compiler flags (`cmake_gcc/xbee_provision.cmake`): C17, `-Wall -Wextra -Os -fdata-sections
  -ffunction-sections`, `--specs=nano.specs`, linker `--gc-sections`.
- `app.c` is compiled in the generated `slc` OBJECT library, which does not inherit
  `xbee_provision` target settings (see `docs/history/2026-09-17-vcom-printf.md`).

## Design

### Levels (`src/utils/inc/app_log.h`)
Plain integer `#define`s, so the preprocessor can compare them (an enum can't be used in `#if`):

```c
#define APP_LOG_LEVEL_VERBOSE  0U
#define APP_LOG_LEVEL_INFO     1U
#define APP_LOG_LEVEL_WARNING  2U
#define APP_LOG_LEVEL_ERROR    3U
#define APP_LOG_LEVEL_NONE     4U   // filter value only: disables all output

typedef uint8_t app_log_level_t;
```

### Configuration (`src/utils/inc/app_log_config.h`, new)

```c
#ifndef APP_LOG_LEVEL_MIN
#define APP_LOG_LEVEL_MIN      APP_LOG_LEVEL_INFO   // compile-time floor
#endif

#ifndef APP_LOG_COLOR_ENABLE
#define APP_LOG_COLOR_ENABLE   1                    // 0 = plain text
#endif
```

- Default floor: INFO (confirmed at plan approval). VERBOSE calls are compiled out by default.
- Per-build override: add a define (for example `APP_LOG_LEVEL_MIN=0`) under
  `# Add additional macros here` in `cmake_gcc/CMakeLists.txt`. The define must also reach the
  `slc` target. See Risks.

### Public API (`src/utils/inc/app_log.h`)
- Per-file tag: `#define APP_LOG_TAG "app"` before `#include "app_log.h"`. If it is not defined,
  the tag defaults to `"-"`.
- Level macros: `APP_LOG_VERBOSE(fmt, ...)`, `APP_LOG_INFO(fmt, ...)`,
  `APP_LOG_WARNING(fmt, ...)`, `APP_LOG_ERROR(fmt, ...)`.
  - Level enabled at compile time (`LEVEL >= APP_LOG_LEVEL_MIN`): expands to
    `(void)app_log_write(LEVEL, APP_LOG_TAG, fmt APP_LOG_COLOR_RESET "\n", ##__VA_ARGS__)`.
    The colour reset and newline are joined to the format string at compile time.
  - Level disabled: expands to `do { if (0) { (void)app_log_write(...); } } while (0)`.
    Arguments are never evaluated, printf format checking stays on, and variables used only in
    log calls cause no `-Wunused` warnings. The call and its string are expected to be removed by
    `-Os` and `--gc-sections`, which was checked in the compiled objects (see Verification).
- `sl_status_t app_log_write(app_log_level_t level, const char *tag, const char *fmt, ...)`
  with `__attribute__((format(printf, 3, 4)))`. It is the macro back end and should not be called
  directly.
- `sl_status_t app_log_set_level(app_log_level_t level)`
  - `SL_STATUS_INVALID_PARAMETER` if `level > APP_LOG_LEVEL_NONE`;
  - `SL_STATUS_INVALID_RANGE` if `level < APP_LOG_LEVEL_MIN` (compiled-out levels cannot be
    restored); the level stays unchanged;
  - otherwise stores the level and returns `SL_STATUS_OK`.
- `app_log_level_t app_log_get_level(void)`
- Documented constraint: thread (super-loop) context only.

### Implementation (`src/utils/src/app_log.c`)
- `static app_log_level_t runtime_level = APP_LOG_LEVEL_MIN;` It is accessed from thread context
  only, so no locking is needed.
- `static const` tables indexed by level:
  - letters `'V'`, `'I'`, `'W'`, `'E'`;
  - colours when `APP_LOG_COLOR_ENABLE` is set: VERBOSE `"\x1B[90m"` (grey), INFO `""`,
    WARNING `"\x1B[33m"` (yellow), ERROR `"\x1B[31m"` (red). `APP_LOG_COLOR_RESET` is
    `"\x1B[0m"`, or `""` when colour is disabled.
- `app_log_write()` sequence:
  1. `tag` or `fmt` is NULL -> `SL_STATUS_NULL_POINTER`. `level >= APP_LOG_LEVEL_NONE` ->
     `SL_STATUS_INVALID_PARAMETER`.
  2. `level < runtime_level` -> return `SL_STATUS_OK`, no output.
  3. `CORE_InIrqContext()` -> return `SL_STATUS_INVALID_STATE`, no output. This guard exists
     because the stdio write blocks.
  4. Timestamp: `sl_sleeptimer_tick64_to_ms(sl_sleeptimer_get_tick_count64(), &ms)`. Split it
     into `unsigned long` seconds (`ms / 1000`) and milliseconds (`ms % 1000`), which avoids
     `%llu` under newlib-nano. If the conversion fails, print `0.000` and continue.
  5. `printf("%s[%6lu.%03lu] %c %s: ", colour, sec, msec, letter, tag)`, then
     `vprintf(fmt, args)`. Line-buffered stdout sends the prefix and message in one flush at `\n`.
  6. Either printf returns `< 0` -> `SL_STATUS_FAIL`. Otherwise `SL_STATUS_OK`.
- Example line: `[     1.234] I app: xbee_provision boot, built Sep 17 2026 00:30:00`

### Removal of the printf demo (done first)
- `app.c` restored to its project-init content (empty `app_init()` / `app_process_action()`).
- `src/utils/inc/app_log.h` and `src/utils/src/app_log.c` deleted. Their source and include lines,
  and the `target_include_directories(slc PUBLIC ...)` block, removed from
  `cmake_gcc/CMakeLists.txt`. The build was checked in this state.
- `app.c` does not use the new module (user decision). The `slc` include block is therefore not
  re-added. `CLAUDE.md` already says to add it when `app.c` includes a layer header.

### Build registration
- `../src/utils/src/app_log.c` under `# Add additional sources here` and `../src/utils/inc` under
  `# Add additional include paths here` (re-registered after the removal).
- A per-build level override under `# Add additional macros here` is not added now. Because
  `app.c` does not log, the question of whether such a define reaches `slc` is deferred to the
  first time `app.c` logs.

### Layer note
`utils` is described as hardware-independent and depending on no project layer. The module now
uses the SDK services sleeptimer (timestamp) and `sl_core` (IRQ check). Neither is a project layer,
so the dependency rule still holds, but the module is no longer purely hardware-independent. It
stays in `utils` so every layer can log. The user can decide whether the layer description should
change.

## Simplicity Studio changes required (PRIME RULE)
None. Sleeptimer (SYSRTC) and the VCOM iostream are already configured.

## Resource impact
Measured against commit dab54ff during implementation.
- Flash: level tables, prefix format and 64-bit tick conversion. Expected well under 1 kB.
  Compiled-out levels should add nothing.
- RAM: 1 byte runtime level. Stack: one nested `printf`/`vprintf` call chain, as today.
- Line length: about 25 more bytes of prefix, plus up to 9 bytes of colour codes. That is about
  3 ms of blocking per line at 115200 baud.
- Energy: unchanged (EM0, no power manager).

## Risks and open questions
- A CMake define added to `target_compile_definitions(xbee_provision PUBLIC ...)` probably does
  not reach the `slc` OBJECT library, which compiles `app.c` (same cause as the earlier
  include-path issue). To be verified. If confirmed, the user chooses the fix.
- ANSI escape codes show up as raw text in terminals without ANSI support. Set
  `APP_LOG_COLOR_ENABLE` to `0` there.
- Logs from IRQ or driver-callback context are dropped (with `SL_STATUS_INVALID_STATE`) by design.
- Timestamp accuracy follows LFRCO (SYSRTC clock source).
- Every line now pays the prefix cost. Blocking output stays unsuitable for time-critical paths.

## Verification
Done (build and compile-level):
- Removal state: `cmake --workflow --preset project` exit 0, no warnings. `arm-none-eabi-size`
  text 24164 / data 372 / bss 261772, the same as before the demo.
- With the new module: exit 0, `app_log.c` compiled with `-Wall -Wextra`, no warnings. Image size
  unchanged (24164 / 372 / 261772), because nothing references the module yet and
  `--gc-sections` drops it.
- Compile-level test: a scratch test file (outside the repo) calling every macro. Compiled with
  the project's exact flags from `build/compile_commands.json`, for `APP_LOG_LEVEL_MIN` 0..4 and
  `APP_LOG_COLOR_ENABLE` 1/0. `app_log.c` was compiled for the same 10 combinations. Results: no
  warnings in any of the 20 compiles, including a variable used only in log calls and an
  unbraced `if`/`else` around log macros. Strings kept in the object: level 0 all four,
  level 1 INFO/WARNING/ERROR, level 2 WARNING/ERROR, level 3 ERROR, level 4 none.
  `APP_LOG_LEVEL_MIN=5` stops the build with the `#error`.
- `app_log.o` size: text 266 B with colour, 248 B without. When the module is first used, the
  printf family adds about 4.6 kB (measured for the earlier demo).

Pending:
- On target: when a module first logs, check the line format, colours, timestamps and runtime
  `app_log_set_level()` on VCOM 115200 8N1.
