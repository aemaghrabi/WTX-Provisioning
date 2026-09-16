# Simple printf over VCOM

Date: 2026-09-17
Related plan: docs/plan/2026-09-17-vcom-printf.md

## Summary
Added an `app_log` module in `src/utils` that wraps `printf` over the VCOM iostream. `app.c` now
prints a boot banner and a `heartbeat <n>` line every 1 s, driven by a sleeptimer flag. Also
exposed `src/utils/inc` to the generated `slc` target, and updated `CLAUDE.md` to allow that.

## Motivation
The user asked for simple printf output over VCOM from the EFM32PG28, as the base for debug output
during provisioning work.

## Changes
| File | Change |
| --- | --- |
| `src/utils/inc/app_log.h` | New. `app_log_printf()` (printf format-checked) and the `APP_LOG()` line macro. |
| `src/utils/src/app_log.c` | New. `vprintf` wrapper that returns `sl_status_t`. |
| `src/utils/inc/.gitkeep`, `src/utils/src/.gitkeep` | Removed (folders now hold real files). |
| `app.c` | Boot banner in `app_init()`. 1 s periodic sleeptimer whose IRQ callback sets a flag. `app_process_action()` reads and clears the flag atomically and prints the heartbeat. |
| `cmake_gcc/CMakeLists.txt` | Added `../src/utils/src/app_log.c` and `../src/utils/inc` in the `# Add additional` sections. Added `target_include_directories(slc PUBLIC ../src/utils/inc)`. |
| `CLAUDE.md` | "Allowed to edit" and the build registration rule now cover the `slc` include block. |
| `docs/plan/2026-09-17-vcom-printf.md` | New plan. |
| `docs/history/2026-09-17-vcom-printf.md` | This entry. |

Why the `slc` include block: `app.c` is compiled in the generated `slc` OBJECT library
(`cmake_gcc/xbee_provision.cmake`), which does not inherit include paths added to the
`xbee_provision` target. The first build failed with
`app.c:25:10: fatal error: app_log.h: No such file or directory`. The user chose the `slc` block
over a `.slcp` include path or a relative `#include`.

## Simplicity Studio changes (made by the user)
| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |
| Software Components | IO Stream: EUSART, instance `VCOM` | Convert `\n` to `\r\n` | 0 | 1 (commit e1daf66) |
| Software Components | Sleep Timer | Timer Peripheral Used by Sleeptimer | Default (auto select, resolves to SYSRTC) | SYSRTC (commit e439304) |

## Verification
Build only. The code has not been run on target yet.
- `cmake --workflow --preset project`: exit 0. `app.c` and `app_log.c` were recompiled with
  `-Wall -Wextra` and produced no warnings or errors.
- `xbee_provision.map` contains `app_log_printf` and `heartbeat_cb`.
- `arm-none-eabi-size`: text 24164 -> 28808 B (+4644), data 372 -> 468 B (+96),
  bss 261772 -> 261676 B (-96).
- Static review: no Studio-owned file was edited by Claude. No logging in IRQ context.
  The sleeptimer start status is checked and logged.

## Known limitations and follow-ups
- On-target check pending. Open the J-Link VCOM at 115200 8N1 and expect the banner, then
  `heartbeat 1`, `heartbeat 2`, ... once per second. Plan status stays `In progress` until this is
  observed.
- `printf` blocks the super loop while writing (about 1 ms per heartbeat line). Use it for debug
  output only.
- `%f` is not available with newlib-nano unless `-u _printf_float` is linked.
- Heartbeat timing uses LFRCO. Enable LFXO in Studio if accurate long timeouts are needed later.
- `docs/plan/2026-09-17-project-folder-structure.md` still describes CMake registration without
  the `slc` include block. `CLAUDE.md` is now the current rule.
