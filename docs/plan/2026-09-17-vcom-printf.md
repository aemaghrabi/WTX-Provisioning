# Simple printf over VCOM

Status: Done (verified on target; demo code later removed, see docs/plan/2026-09-17-multi-level-log.md)
Date: 2026-09-17
Related history: docs/history/2026-09-17-vcom-printf.md

## Goal
Prove that `printf()` output from the EFM32PG28 reaches a host terminal over the VCOM UART, as
the base for provisioning debug output:
- one boot banner printed from `app_init()`;
- one heartbeat line (`heartbeat <n>`) per second from `app_process_action()`, driven by a
  non-blocking sleeptimer tick;
- output goes through a small `app_log` module in `src/utils`.

The host link is the debugger VCOM for now and an FTDI USB-UART on the same pins later.

## Background and references
- `config/sl_iostream_eusart_VCOM_config.h`: EUSART2, 115200 8N1, no flow control, TX PD08,
  RX PD07 (swapped by the user in commit dab54ff; originally TX PD07, RX PD08), high-frequency mode, RX buffer 32, LF->CRLF conversion enabled, restrict EM for RX = 1.
- `autogen/sl_event_handler.c`: `sl_service_init()` calls `sl_iostream_eusart_init_instances()`
  and `sl_iostream_set_console_instance()`. SDK `sl_iostream_uart.c:283` sets the VCOM stream as
  the system default stream.
- SDK `platform/service/iostream/src/sl_iostream_retarget_stdio.c`: `_write()` ->
  `sl_iostream_write(SL_IOSTREAM_STDOUT, ...)` -> default stream (VCOM). `_isatty()` is provided,
  so newlib-nano (`--specs=nano.specs`) keeps stdout line-buffered. A line flushes only on `\n`
  (or `fflush`). `%f` is not supported with nano specs unless `-u _printf_float` is linked (not
  needed here).
- `autogen/sl_component_catalog.h`: `SL_CATALOG_SLEEPTIMER_PRESENT`. SDK
  `platform/service/sl_main/src/sl_main_init.c` calls `sl_sleeptimer_init()` inside
  `sl_main_init()`, so app code only starts timers.
- SDK `platform/service/sleeptimer/inc/sli_sleeptimer.h`: with the `DEFAULT` peripheral setting,
  the auto-select order is RTCC, RTC, SYSRTC, BURTC, WTIMER, TIMER. EFM32PG28B210F1024IM68 has no
  RTCC or RTC, so it would pick SYSRTC.
- `docs/manuals/efm32pg28_reference_manual.md`, SYSRTC introduction: 32-bit counter that keeps
  running down to EM3, meant as a sleep timer and wakeup source. The memory map lists SYSRTC0
  as EM2 (PD0A). BURTC is the EM4 backup RTC.
- `config/sl_clock_manager_tree_config.h` / `sl_clock_manager_oscillator_config.h`: SYSRTCCLK uses
  the default LF source, which is LFRCO. LFXO is disabled.
- No `power_manager` component: the super loop never sleeps, so the device stays in EM0.

## Design

### Module `app_log` (layer `utils`)
`src/utils/inc/app_log.h` (Doxygen header, guard `APP_LOG_H`, 2-space indentation):
- `sl_status_t app_log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));`
- `#define APP_LOG(fmt, ...) app_log_printf(fmt "\n", ##__VA_ARGS__)`. Every line ends with
  `\n`, so line-buffered stdout flushes it, and the iostream turns it into `\r\n`.
- Documented constraint: call from thread (super-loop) context only. Never call from an IRQ or
  driver callback, because the stream write blocks until the bytes are handed to EUSART.
- Includes only `<stdarg.h>`, `<stdio.h>`, `sl_status.h`. No project-layer includes, which meets
  the `utils` rule.

`src/utils/src/app_log.c`:
- A `vprintf()` wrapper. Returns `SL_STATUS_NULL_POINTER` for a `NULL` format,
  `SL_STATUS_FAIL` if `vprintf()` returns `< 0`, otherwise `SL_STATUS_OK`.

### Application (`app.c`)
- `#include "app.h"`, `"app_log.h"`, `"sl_sleeptimer.h"`, `"sl_core.h"` (the SDK 2025.6 core
  header in `platform/common/inc`).
- `#define APP_HEARTBEAT_PERIOD_MS 1000U`
- File statics: `sl_sleeptimer_timer_handle_t heartbeat_timer`, `volatile bool heartbeat_due`,
  `uint32_t heartbeat_count`.
- `heartbeat_cb()` runs in sleeptimer IRQ context. It only sets `heartbeat_due = true`.
- `app_init()`:
  1. `APP_LOG("xbee_provision boot, built " __DATE__ " " __TIME__)`
  2. `sl_sleeptimer_start_periodic_timer_ms(&heartbeat_timer, APP_HEARTBEAT_PERIOD_MS,
     heartbeat_cb, NULL, 0, 0)`. If the status is not `SL_STATUS_OK`, log the code.
     There is no retry, and the banner still proves the output path.
- `app_process_action()`: read and clear `heartbeat_due` between `CORE_ENTER_ATOMIC()` and
  `CORE_EXIT_ATOMIC()` (with `CORE_DECLARE_IRQ_STATE`). If it was
  set, `APP_LOG("heartbeat %lu", (unsigned long)++heartbeat_count)`. Otherwise return at once.

### Timing
- Heartbeat period 1000 ms, from SYSRTC (LFRCO-clocked). The flag is served on the next super-loop
  pass.
- One heartbeat line is about 13 bytes, including CRLF. At 115200 baud (10 bits/byte) the
  blocking write takes about 1.1 ms. The banner is about 50 bytes, about 4.3 ms, once.

### Build registration (`cmake_gcc/CMakeLists.txt`)
- `# Add additional sources here`: `../src/utils/src/app_log.c`
- `# Add additional include paths here`: `../src/utils/inc`
- Remove `src/utils/inc/.gitkeep` and `src/utils/src/.gitkeep`.
- `target_include_directories(slc PUBLIC ../src/utils/inc)` after the `xbee_provision` include
  block. This was added during implementation: `app.c` is compiled in the generated `slc` OBJECT
  library (`cmake_gcc/xbee_provision.cmake`), which does not inherit the `xbee_provision` include
  paths. The first build failed with `app_log.h: No such file or directory`. The user chose this
  fix, and `CLAUDE.md` was updated to allow this block.

## Simplicity Studio changes required (PRIME RULE)
| Tool | Component / instance | Setting | Current value | Required value |
| --- | --- | --- | --- | --- |
| Software Components | IO Stream: EUSART, instance `VCOM` | Convert `\n` to `\r\n` | Enabled (done, commit e1daf66) | Enabled |
| Software Components | Sleep Timer -> Configure | Timer Peripheral Used by Sleeptimer | SYSRTC (done, commit e439304) | SYSRTC |

Both are done. Setting SYSRTC explicitly does not change behaviour, because `DEFAULT` already
resolved to SYSRTC. It makes the config state the choice. Implementation is application code only.

## Resource impact
- Flash: estimated at 1-3 kB. Measured with `arm-none-eabi-size`: text 24164 -> 28808 B (+4644 B),
  mostly newlib-nano `vfprintf` and stdio.
- RAM: data 372 -> 468 B (+96 B), bss 261772 -> 261676 B (-96 B). Not verified: newlib normally
  allocates the stdout buffer from the heap at the first write, so static size would not show it.
- Peripherals: EUSART2 (already initialised by `iostream_eusart`), SYSRTC0 via sleeptimer.
- Interrupts: EUSART2 (iostream), SYSRTC (sleeptimer). Both are already owned by SDK components.
- Energy: EM0 (no power manager). Future note: VCOM "restrict EM to allow reception" = 1 keeps the
  device in EM1 once a power manager is added. Disable it in Studio if VCOM RX is not needed.

## Risks and open questions
- `printf` is blocking, which suits debug output only. Do not call it on time-critical paths or
  from IRQ or driver-callback context.
- LFRCO accuracy is lower than LFXO. That is fine for a debug heartbeat. For accurate long
  timeouts later, enable LFXO in Studio if the board has a 32.768 kHz crystal.
- Output before `sl_service_init()` is lost. Log from `app_init()` onward only.
- Future FTDI link: same pins (PD08 MCU TX -> FTDI RX, PD07 MCU RX <- FTDI TX), common GND,
  3.3 V logic only. No firmware or config change unless the pins move (then Pin Tool).
- Files registered only in `CMakeLists.txt` are not in the `.slcp` (see
  `2026-09-17-project-folder-structure.md`).

## Verification
- Build: `cd cmake_gcc && cmake --workflow --preset project`. Zero warnings, `app_log.c` compiled,
  `xbee_provision.out` links. Record the flash/RAM delta.
- Static review: `git status` shows no Studio-owned file changed by Claude. No `APP_LOG`/`printf`
  in IRQ context. All `sl_status_t` returns checked.
- On target (user): open the J-Link VCOM at 115200 8N1. Expect the banner once after reset, then
  `heartbeat 1`, `heartbeat 2`, ... about once per second, with correct line endings (no
  staircase). Record the result in the history entry as "on target" only if it was observed.
