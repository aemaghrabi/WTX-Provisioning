# VCOM printf demo: on-target result, pin swap, removal

Date: 2026-09-17
Related plan: docs/plan/2026-09-17-vcom-printf.md
Follows: docs/history/2026-09-17-vcom-printf.md

## Summary
The printf demo from commit 84f72a7 was confirmed working on target. The user swapped the VCOM
pins in commit dab54ff. The demo (banner and heartbeat in `app.c`, the simple `app_log` module and
its CMake entries) was then removed, to make way for the multi-level log system.

## Motivation
The demo had served its purpose (proving the VCOM output path). The user asked for it to be
removed before implementing the multi-level log system.

## Changes
| File | Change |
| --- | --- |
| `app.c` | Restored to the project-init content (empty `app_init()` / `app_process_action()`). |
| `src/utils/inc/app_log.h`, `src/utils/src/app_log.c` | Old simple logger deleted. Replaced by the new module in the same change set, see `docs/history/2026-09-17-multi-level-log.md`. |
| `cmake_gcc/CMakeLists.txt` | Removed the `app_log.c` source line, the `src/utils/inc` include line and the `target_include_directories(slc PUBLIC ...)` block. |
| `docs/plan/2026-09-17-vcom-printf.md` | Status -> Done. VCOM pin references updated to the swapped pins. |
| `docs/history/2026-09-17-vcom-printf-followup.md` | This entry. |

The `CLAUDE.md` rule about the `slc` include block is kept. It applies again once `app.c`
includes a `src/` layer header.

## Simplicity Studio changes (made by the user)
| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |
| Pin Tool / IO Stream: EUSART `VCOM` (commit dab54ff) | EUSART2 TX | Pin | PD07 | PD08 |
| Pin Tool / IO Stream: EUSART `VCOM` (commit dab54ff) | EUSART2 RX | Pin | PD08 | PD07 |

The exact Studio tool used for the swap was not recorded. Values come from the diff of
`config/sl_iostream_eusart_VCOM_config.h`, `config/pin_config.h` and `xbee_provision.pintool`.

## Verification
- On target (reported by the user): the printf demo output was seen over VCOM.
- Removal: build only. `cmake --workflow --preset project` exit 0, no warnings.
  `arm-none-eabi-size` text 24164 / data 372 / bss 261772, identical to the pre-demo build.

## Known limitations and follow-ups
- `docs/history/2026-09-17-vcom-printf.md` still lists the verification as "build only" and the
  on-target check as pending. This entry supersedes those lines (history is append-only).
