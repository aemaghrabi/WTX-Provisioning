# Device serial number written to MCU NVM3 (GitHub issue #4)

Date: 2026-10-06
Related plan: docs/plan/2026-10-06-device-serial-number.md

## Summary

The provisioning firmware now writes the device serial number `YYWW-NNNNN-C` to the EFM32's NVM3.
It is stored at key `0x00003` as exactly 13 bytes, where the production firmware (WTX-FW) reads
it. There are two ways to write it:

- **Console build:** a new console command, `device serial-number <NNNNN> [week <YYWW>]`, with
  `show device serial-number` to read it back.
- **Provisioning (hex) build:** `tools/provision.py --sequence NNNNN` builds a per-unit image.
  That image writes the serial number once the XBee configuration has passed.

## Motivation

GitHub issue #4 asks for the serial number to be written in both the console build and the
provisioning build. It specifies:

- the format, digits only with literal dashes;
- the ISO 7064 MOD 11-10 check digit;
- the NVM3 key `0x00003`;
- the exact 13-byte length that WTX-FW requires.

The project owner decided the following:

| Topic | Decision |
| --- | --- |
| Source of NNNNN | The console and the provision script, for now. Tying it to the CI/CD release system comes later. |
| Source of YYWW | The build date, with the ISO week-numbering year. The console may override it. |
| Check digit | The standard ISO 7064 MOD 11-10. |
| Existing value, console | Warn and ask before overwriting. |
| Existing value, provisioning build | Always overwrite. |
| When the provisioning build writes | Only after the XBee configuration passes. |
| Provisioning build with no sequence number | Fails to build. |
| Script | Python. It builds, then asks before it flashes and watches the log. |

## Changes

| File | Change |
| --- | --- |
| `src/utils/inc/device_sn.h`, `src/utils/src/device_sn.c` | New. ISO 7064 MOD 11-10 check digit; ISO week and ISO week-year from a date and from `__DATE__`; parsing of NNNNN and YYWW; the 13-byte formatter; validation of a stored value (shape, then check digit); a printable copy of stored bytes. No hardware dependency. |
| `src/services/inc/nvm_store.h`, `src/services/src/nvm_store.c` | New. Access to the NVM3 default instance. `NVM_STORE_KEY_DEVICE_SN` is `0x00003`. Reads accept only a data object of exactly 13 bytes, which is WTX-FW's rule. A write is refused unless the value passes `device_sn_validate()`, and is read back and compared. |
| `src/app/inc/sn_burner.h`, `src/app/src/sn_burner.c` | New. The provisioning build's serial number step. It runs once, after `xbee_provision_is_finished()`, and writes only if the XBee run passed. It takes its input from `SN_BURNER_SEQUENCE`, which is stringified so a leading zero never becomes octal, and from `SN_BURNER_BUILD_YYWW`, falling back to `__DATE__`. It ends with exactly one `serial number written: ...` or `serial number not written: ...` log line. |
| `app.c` | Calls `sn_burner_process()` after `xbee_provision_process()` in the PROVISION branch. Adds an `#error` when the PROVISION build has no `SN_BURNER_SEQUENCE`. The selection comment now describes `XBEE_APP_SELECT`. |
| `src/app/src/cli.c` | New nodes `device serial-number <NNNNN> [week <YYWW>]` (configuration mode) and `show device serial-number` (every mode). New state `CLI_STATE_SN_CONFIRM`: preview, warning on a different stored value, and a `[N/y]` question where only `y` or `yes` writes. Ctrl-C, overflow and a timeout all write nothing. |
| `src/app/src/cli_show.c`, `src/app/src/cli_show.h` | `cli_show_device_sn()`. |
| `src/app/inc/cli_config.h` | `CLI_SN_CONFIRM_TIMEOUT_MS`, 30000. |
| `cmake_gcc/CMakeLists.txt` | Three new sources. In both `target_compile_definitions` blocks, `XBEE_APP` now comes from `XBEE_APP_SELECT` (default `CLI`, so the default build is unchanged). `SN_BURNER_SEQUENCE` and `SN_BURNER_BUILD_YYWW` are passed only when they are given. |
| `tools/provision.py`, `tools/requirements.txt` | New. Builds the PROVISION variant per unit in `cmake_gcc/build-provision/`, checks the defines in the compile database, and copies the image to `units/xbee_provision_<serial>.hex`. On "y" it flashes with Simplicity Commander and watches VCOM for the result line. Exit codes are documented in the script. pyserial is needed only for the watch step. |
| `test/test_device_sn.c`, `test/CMakeLists.txt` | New host test suite `test_device_sn`. |
| `.gitignore` | `cmake_gcc/build-provision/`. |
| `readme.md` | Scope item 4, component list, application selection (`XBEE_APP_SELECT`), new "Device serial number" section, `tools/` in the layout, and the production flow, including "no mass erase before flashing WTX-FW". |
| `CLAUDE.md` | `tools/**` added to "Allowed to edit". The NVM3 component and the shared NVM3 configuration added to "Project facts". |
| `docs/plan/2026-10-06-device-serial-number.md` | New plan. Status is now "Done, pending on-target verification". |

## Simplicity Studio changes (made by the user)

Made in commit 294dfb2, "Add NVM3 Software Components", before any application code was written.

| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |
| Software Components | NVM3 Default Instance (`nvm3_default`) | Installed | No | Yes |
| Software Components | `nvm3_default_config` | Installed | No | Yes |
| Software Components | NVM3 flash back-end, NVM3 core, `memory_manager` | Installed | No | Yes (resolved as dependencies) |
| NVM3 Default Instance configuration | `NVM3_DEFAULT_NVM_SIZE` | — | — | 40960 (default, same as WTX-FW) |
| NVM3 Default Instance configuration | `NVM3_DEFAULT_CACHE_SIZE` | — | — | 200 (default, same as WTX-FW) |
| NVM3 Default Instance configuration | `NVM3_DEFAULT_MAX_OBJECT_SIZE` | — | — | 254 (default, same as WTX-FW) |
| NVM3 Default Instance configuration | `NVM3_DEFAULT_REPACK_HEADROOM` | — | — | 0 (default, same as WTX-FW) |

Regenerated results:

- `config/nvm3_default_config.h` is new.
- `autogen/sl_event_handler.c` now calls `nvm3_initDefault()` in `sl_platform_init()`.
- The `memory_manager` dependency renamed the linker heap section to `.memory_manager_heap` and
  added `config/sl_memory_manager_config.h`.

## Verification

Build, static review, and compile-only checks. **Nothing was run on hardware.**

- **Console build.** A clean rebuild (`cmake --build build --config base --target xbee_provision
  --clean-first`) gives 0 warnings and 0 errors under `-Wall -Wextra -std=c17 -Os`. The image is
  text 88772 B, data 520 B, `.bin` 89296 B.
- **Provisioning build.** `python tools/provision.py --sequence 00123`, answering N, completed
  as follows:
  - It printed `2641-00123-3` (built on 2026-10-06, ISO week 2026-W41).
  - It built in `cmake_gcc/build-provision/` with 0 warnings.
  - It confirmed from the compile database that `sn_burner.c` was compiled with
    `-DXBEE_APP=XBEE_APP_PROVISION -DSN_BURNER_SEQUENCE=00123 -DSN_BURNER_BUILD_YYWW=2641`.
  - It wrote `units/xbee_provision_2641-00123-3.hex` and exited 0.
  - The image holds the bytes `"2641\0"` and `"00123\0"` (`objdump` at 0x0800F530) and all five
    result log strings.
  - The image is text 71316 B, `.bin` 71832 B.
- **Negative checks:**
  - Configuring `XBEE_APP_SELECT=PROVISION` without a sequence number stops at the `#error` in
    `app.c`.
  - The script rejects `--sequence 12a45` and `0012` with exit code 1.
  - The script's define check reports a wrong NNNNN, a wrong week and a missing compile
    database.
- **NVM3 placement.**
  - Both maps have `.nvm` of 0xA000 bytes and `__nvm3Base = 0x080F4000`.
  - Each `.bin` ends where the dummy `.nvm` section starts, so the NVM3 region is not part of
    either image.
  - WTX-FW has the same `NVM3_DEFAULT_*` values and the same FLASH length (0xFE000) in its linker
    file. That gives the same base, but it was checked from source only; WTX-FW was not built
    here.
- **SDK.** `nvm3_open()` (SDK 2025.6.2, `nvm3.c`) returns `SL_STATUS_OK` without touching flash
  when the instance is already open with the same parameters. That is what lets
  `nvm_store_init()` call `nvm3_initDefault()` again.
- **Algorithms.**
  - The C ISO-week logic, ported line for line to Python, matched `date.isocalendar()` on all
    36525 days from 2000-01-01 to 2099-12-31. The check digit matched the standard's vector,
    `0794` -> 5.
  - `tools/provision.py` gives the same check digits and weeks as the C test vectors.
- **Host tests.** `test/test_device_sn.c` was **not run**: this machine has no host C compiler.
  The SLT clang is an Arm-only build with no x86-64 target and no C library. It and
  `src/utils/src/device_sn.c` were compiled with the ARM GCC under `-std=c11 -Wall -Wextra -Werror
  -Wconversion -Wsign-conversion`, with no diagnostics. While writing the test, one expected value
  was wrong (`0001-00000-2`); the Python reference gives `0001-00000-8`, and the test was
  corrected before the compile check.

Resource impact, attributed per object file from the linker maps:

| Part | Console build flash / RAM | Provisioning build flash / RAM |
| --- | --- | --- |
| NVM3 driver, from the Studio change | 8483 B / 1928 B (cache 1600 B, handle 104 B) | 8483 B / 1928 B |
| `memory_manager`, from the Studio change | 2671 B / 41 B | 1949 B / 41 B |
| `device_sn` | 1274 B / 0 | 734 B / 0 |
| `nvm_store` | 516 B / 0 | 516 B / 0 |
| `sn_burner` | not linked | 1158 B / 2 B |
| `cli.c` additions | part of `cli.c`'s 9985 B; RAM 26 B for the two serial number buffers | not linked |

The 40 KB NVM3 region at `0x080F4000`-`0x080FDFFF` lies outside the image. No energy-mode
change.

## Known limitations and follow-ups

- **On-target verification has not been run** (plan, Verification step 4):
  - the console commands;
  - the script's flash-and-watch, on a pass and on a forced XBee failure;
  - the hand-over to WTX-FW without a mass erase;
  - NVM3 format compatibility between SDK 2025.6.2 (here) and 2025.12.3 (WTX-FW).
- **Host tests:** run `ctest --test-dir test/build` on a machine with a host compiler.
- **WTX-FW read code:** the local WTX-FW checkout does not yet contain the serial number read
  code (`NVM3_KEY_DEVICE_SN` is not in its history). Until it does, check the hand-over with
  Simplicity Commander's NVM3 read/parse. Confirm the exact syntax with `commander nvm3 --help`.
- **Issue #4 example (recommendation):** the example in issue #4 (`2640-00123-4`) has the wrong
  check digit; the correct value is `2640-00123-8`. Correct it there and in the WTX-FW
  specification text. Neither was edited.
- **`CLAUDE.md` allowed sections (recommendation):** "Allowed to edit" lists the
  `target_include_directories(slc ...)` block of `cmake_gcc/CMakeLists.txt`, but not the
  application-owned `target_compile_definitions(slc ...)` block. This work changed that block, as
  the approved plan required. Add it to the list.
- **`readme.md` status line:** it still says the project "contains the Simplicity Studio skeleton
  only". That was already stale and was not changed.
- **Console help alignment:** `serial-number` is wider than `CLI_HELP_TOKEN_WIDTH` (10), so its
  `?` help line is not aligned with the others. Cosmetic.
- **Planned:** a non-interactive option in `tools/provision.py` for CI/CD, NNNNN allocated by the
  release system, and replacing the `XBEE_APP_SELECT` input with the build preset from
  `feat/provision-build-preset`.
