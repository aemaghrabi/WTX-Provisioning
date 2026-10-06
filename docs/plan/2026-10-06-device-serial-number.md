# Device serial number (S/N) in MCU NVM3

Status: Done, pending on-target verification
Date: 2026-10-06
Related history: docs/history/2026-10-06-device-serial-number.md
Related issue: GitHub #4, "Feat. Add Serial Number in MCU NVM"

Implemented as designed. The Studio change was made by the owner in commit 294dfb2.
Verification so far:

- Both the CLI build and the per-unit provisioning build compile and link with no warnings under
  `-Wall -Wextra`.
- A provisioning build without a sequence number stops at the `#error`.
- `__nvm3Base` is `0x080F4000` in both images.
- The host tests were written, but not run: this machine has no host C compiler.
- Verification step 4 (on target) has not been run.

Implementation notes, beyond the design below:

- `tools/provision.py` configures with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. After the build it
  checks that `sn_burner.c` was compiled with `XBEE_APP_PROVISION` and the expected
  `SN_BURNER_SEQUENCE` and `SN_BURNER_BUILD_YYWW`. If that cannot be confirmed, it stops with
  exit code 2.
- The script also takes `--cmake`. Without it, the script uses cmake from `PATH`, then the cmake
  recorded in `cmake_gcc/build/CMakeCache.txt`, then the newest one under `~/.silabs/slt`.
- `readme.md`: the paragraph on choosing the application was rewritten for `XBEE_APP_SELECT`,
  because this work changed that mechanism. The rewrite also corrects its stale default.
- `device_sn` gained `device_sn_to_printable()`. The console and the hex-build log use it to print
  a stored value that may not be text.

## Goal

Both the CLI build and the automatic provisioning (hex) build write the device serial number (the
enclosure nameplate S/N) into the EFM32PG28's NVM3, so that the production firmware (`WTX-FW`)
finds it there.

Measurable outcome:

- **CLI build:** `device serial-number <NNNNN>` in global configuration mode stores
  `YYWW-NNNNN-C` at NVM3 key `0x00003` after the operator confirms, and
  `show device serial-number` reads it back.
- **Hex build:** a per-unit image built by `tools/provision.py --sequence NNNNN` writes the S/N
  after XBee provisioning passes, and ends with the log line
  `serial number written: YYWW-NNNNN-C`.
- **Hand-over:** after WTX-FW is flashed without a mass erase, WTX-FW reads the same 13-byte
  object.

The NNNNN part is passed in through the CLI and the provision script for now. Allocating it from
the CI/CD release system is later work and is not part of this plan.

### Specification (from issue #4)

| Item | Value |
| --- | --- |
| Format | `YYWW-NNNNN-C`, digits only, with the dashes stored as literal characters |
| `YYWW` | 2-digit year plus 2-digit ISO week of manufacture |
| `NNNNN` | 5-digit sequence, 00000 to 99999 |
| `C` | ISO 7064 MOD 11-10 check digit over the 9 digits `YYWWNNNNN` |
| Storage | NVM3 **data** object (`nvm3_writeData()`), key **`0x00003`** |
| Length | **Exactly 13 bytes**: the 12 characters plus one `0x00`. WTX-FW treats any other length as "not set". |

WTX-FW only reads the value. It does not check the check digit or the format, so the provisioning
side alone is responsible for getting `C` right.

### Behaviour decided by the project owner

| Topic | Decision |
| --- | --- |
| Source of `YYWW` | The build date. The CLI can override it with `week <YYWW>`. |
| Year at a year boundary | The ISO week-numbering year. 2027-01-01 is in ISO week 53 of 2026, so it gives `2653`. |
| Check digit | Standard ISO 7064 MOD 11-10 (see "Risks and open questions", item 1). |
| S/N already stored, CLI build | Warn, show the stored value, and ask permission before overwriting. |
| S/N already stored, hex build | Always overwrite. |
| Order in the hex build | Write the S/N only after XBee provisioning has passed. |
| Hex build with no NNNNN given | The build fails with `#error`. |
| Selecting the PROVISION variant | For now, an `XBEE_APP_SELECT` input in the existing CMake macro sections. The build preset (`feat/provision-build-preset`) replaces it later. |
| CLI syntax | `device serial-number <NNNNN> [week <YYWW>]` in configuration mode. `show device serial-number` in every mode. |
| Provision script | `tools/provision.py` (Python), with `tools/requirements.txt` (pyserial). It builds, then asks before it flashes and watches the log. |

## Background and references

- `docs/manuals/efm32pg28_reference_manual.md`, section 5.3 (lines 2220-2234): main flash is
  mapped at `0x08000000` and organised in 8 kB pages, 128 pages on the 1024 kB part.
- **WTX-FW** (`../WTX-FW`), the reader of the value:
  - `TWS_V1.slcp` installs `nvm3_default` and `nvm3_default_flash_backend`.
  - `config/nvm3_default_config.h`: NVM size 40960, cache 200, max object size 254, repack
    headroom 0.
  - `lcd_menu.c` uses NVM3 keys `0x00001` (settings) and `0x00002` (radio state). Issue #4 names
    `0x00003` as `NVM3_KEY_DEVICE_SN`, read by `menu_get_device_sn()` with
    `DEVICE_SN_STR_LEN` = 13 (`lcd_menu.h`).
  - The local WTX-FW checkout does not contain that read code yet: `NVM3_KEY_DEVICE_SN` does not
    appear in its git history.
  - WTX-FW is built with Simplicity SDK 2025.12.3.
- **Both linker files** (`autogen/linkerfile.ld` here, `WTX-FW/autogen/linkerfile.ld`):
  - FLASH ends at `0x080FE000` (`LENGTH = 0xfe000`).
  - NVM3 is placed at `linker_nvm_end - SIZEOF(.nvm)`.
  - With 40960 bytes in both, `__nvm3Base` is `0x080F4000` in both images.
- **SDK 2025.6.2** (this project), `platform/emdrv/component/nvm3_default.slcc`:
  - With `sl_main`, the default instance is opened by `nvm3_initDefault()` during `platform_init`.
  - The defaults in `platform/emdrv/nvm3/config/s2/nvm3_default_config.h` are the same four
    values WTX-FW uses.
- **This project today:**
  - There is no NVM3 component. `em_msc.c` is compiled but nothing calls it.
- **Name clash:** `SN` is already the XBee AT command "Number of Sleep Periods"
  (`src/services/inc/xbee_at_table.h`, `src/app/src/xbee_provision_list.h`). That is why the
  console keyword is `device serial-number`.
- **Variant selection:** `XBEE_APP=XBEE_APP_CLI` is hardcoded in both `target_compile_definitions`
  blocks of `cmake_gcc/CMakeLists.txt`.
- **Build date:** `show version` already prints `__DATE__` (`src/app/src/cli.c`).
- **Parser:** a node may carry a command ID and also have children (`cli_parser_parse()`,
  `src/services/src/cli_parser.c`). That is what allows an optional trailing `week <YYWW>`.

## Design

### Layering

| Layer | Module | Role |
| --- | --- | --- |
| utils | `device_sn` | Format, check digit, ISO week, input parsing. Pure C, testable on the host. |
| services | `nvm_store` | NVM3 default-instance access and the key table. |
| app | `sn_burner` | The hex build's S/N step, which runs after XBee provisioning. |
| app | `cli.c` / `cli_show.c` | The console commands. |

Dependencies point downward only: app -> services -> utils, and `nvm_store` -> SDK `nvm3`.

### utils: `src/utils/inc/device_sn.h`, `src/utils/src/device_sn.c`

- `DEVICE_SN_STR_LEN` (13U) must equal WTX-FW's `DEVICE_SN_STR_LEN`. The digit counts are named
  constants beside it (YYWW 4, sequence 5).
- **Check digit**, ISO 7064 MOD 11-10, over an ASCII digit string:
  - P = 10.
  - For each digit d: S = (P + d) mod 10, with 0 replaced by 10, then P = (2 × S) mod 11.
  - C = (11 − P) mod 10.
- **ISO week from a date** (year, month, day) gives the ISO week-year and week. Week 53 is
  accepted only for long years (1 January a Thursday, or a leap year whose 1 January is a
  Wednesday).
- **Build date:** parse the `__DATE__` form `"Mmm dd yyyy"`, where the day is space-padded, into
  YYWW.
- **Sequence:** exactly 5 ASCII digits.
- **YYWW text:** exactly 4 digits, WW from 01 to 53, and the week must exist in ISO year `20YY`.
- **Format:** YYWW and the sequence become `YYWW-NNNNN-C` plus the terminating NUL, in a
  caller-supplied 13-byte buffer.
- **Validity check of a stored string:** the shape (dashes at offsets 4 and 10, digits
  elsewhere, NUL at 12) and the check digit.
- Every function returns `sl_status_t` and checks its pointers and lengths. Nothing is
  allocated.

### services: `src/services/inc/nvm_store.h`, `src/services/src/nvm_store.c`

- `NVM_STORE_KEY_DEVICE_SN` is `0x00003`. A comment ties it to WTX-FW `NVM3_KEY_DEVICE_SN`. Keys
  `0x00001` and `0x00002` belong to WTX-FW, and this firmware never touches them.
- **Init:** confirm that the default instance is open (the `sl_main` init ignores the return
  value). The SDK documentation is checked before calling `nvm3_initDefault()` a second time.
- **Read:**
  - Call `nvm3_getObjectInfo()` first.
  - Accept only an `NVM3_OBJECTTYPE_DATA` object of exactly 13 bytes; this is WTX-FW's own rule.
  - No object gives `SL_STATUS_NOT_FOUND`.
  - A wrong length gives `SL_STATUS_INVALID_COUNT`, and the stored length is reported so that
    `show` can print it.
- **Write:**
  - Call `nvm3_writeData()` with exactly 13 bytes.
  - Read the object back and compare it byte for byte.
  - Every `Ecode_t` is logged and mapped to an `sl_status_t`.
- **Blocking:** the write is synchronous. It consists of word writes and, at worst, one 8 kB page
  erase when NVM3 repacks. A comment justifies this against the non-blocking rule: it is a
  one-time factory step that runs while nothing else is in flight.

### app, hex build: `src/app/inc/sn_burner.h`, `src/app/src/sn_burner.c`

- **Build inputs:**
  - `SN_BURNER_SEQUENCE` holds the 5 digits, passed unquoted and turned into a string with the
    preprocessor (`#x`). A leading zero is therefore never read as an octal number, and `00089`
    still compiles. A `_Static_assert` checks that the string is 5 characters long. The digits
    are checked again at runtime.
  - `SN_BURNER_BUILD_YYWW` is optional. When it is absent, the week comes from `__DATE__`.
- **`sn_burner_process()`** is called from `app.c` right after `xbee_provision_process()` in the
  PROVISION branch.
  - It does nothing until `xbee_provision_is_finished()`, then runs exactly once.
  - **If `xbee_provision_passed()`:**
    1. Format the S/N.
    2. Read the stored value and log a WARNING with the old value when it differs.
    3. Write, which always overwrites.
    4. Verify.
  - **Otherwise:** it writes nothing.
- **Terminal log line.** Exactly one of these is printed per boot, and the script matches on it:
  - `serial number written: YYWW-NNNNN-C`
  - `serial number not written: <reason>`, where the reason is one of: XBee provisioning
    failed, invalid build input, or NVM error.
- **`app.c`:** the PROVISION branch has `#ifndef SN_BURNER_SEQUENCE` -> `#error`. The message
  names `tools/provision.py` and `-DSN_BURNER_SEQUENCE=NNNNN`.

### app, CLI build: `src/app/src/cli.c`, `src/app/src/cli_show.c`

**Command tree:**

- **Configuration mode (`M_CONF`):**
  - `device` -> `serial-number` -> `<NNNNN>`, where `<NNNNN>` carries `CMD_DEVICE_SN_SET`.
  - Optional continuation: `week` -> `<YYWW>`, which also carries `CMD_DEVICE_SN_SET`. The
    argument count tells the two forms apart.
- **Every mode:** `show device serial-number`, which carries `CMD_SHOW_DEVICE_SN`.
- The hand-kept `child_count` values are updated.

**`device serial-number` flow:**

1. Validate NNNNN (and YYWW when given). Bad input gets a `% ` error and changes nothing.
2. Take YYWW from `week` or from `__DATE__`, then format the S/N and read the stored value.
3. Preview, for example `Serial number 2641-00123-3 (week 2641 from build date)`.
4. What is asked depends on what is stored:
   - **Nothing stored, or a wrong length:** `Write to MCU NVM? [N/y]`
   - **A different value:** `% Warning: 2638-00007-1 is already stored. Overwrite with 2641-00123-3? [N/y]`
   - **The same value:** `already stored`. Nothing is written and no question is asked.
5. A new state, `CLI_STATE_SN_CONFIRM`, collects the answer. It is modelled on the save prompt
   of `xbee provision all` (`handle_save_confirm` / `decide_save`): the default is No, and the
   prompt has a timeout.
6. On "y": write and verify, then print `[OK] serial number written: ...` or a `% ` error.

**`show device serial-number`** prints one of:

- the value
- `not set`
- `stored object has wrong length (N bytes), treated as not set`
- the value followed by `(check digit invalid)`

### Build: `cmake_gcc/CMakeLists.txt` (only the sections CLAUDE.md allows)

- **`# Add additional sources here`:**
  - `../src/utils/src/device_sn.c`
  - `../src/services/src/nvm_store.c`
  - `../src/app/src/sn_burner.c`
- **Both macro blocks** (`xbee_provision` and `slc`), each with an empty-string test so that a
  value of `00000` is not taken as false:
  - `XBEE_APP=XBEE_APP_$<IF:$<STREQUAL:${XBEE_APP_SELECT},>,CLI,${XBEE_APP_SELECT}>`. Without
    the input, the build stays CLI as it is today.
  - `$<$<NOT:$<STREQUAL:${SN_BURNER_SEQUENCE},>>:SN_BURNER_SEQUENCE=${SN_BURNER_SEQUENCE}>`
  - The same pattern for `SN_BURNER_BUILD_YYWW`.
- **Include paths:** none new. The utils, services and app `inc/` directories are already listed.

### Build date

`__DATE__` changes only when a file is recompiled. A second run of the script with the same
NNNNN on a later day would otherwise keep the old week. For that reason the script passes the
date of the build, taken from the build host's `date.isocalendar()`, as `SN_BURNER_BUILD_YYWW`.
This is still the build date, and a change of date forces a recompile. CLI images built without
the script use `__DATE__`, which is the date `show version` prints. The operator can override it
with `week`.

### Provision script: `tools/provision.py`, `tools/requirements.txt`

1. **Arguments:**
   - `--sequence NNNNN`: required, matched against `^\d{5}$`.
   - `--build-dir`: default `cmake_gcc/build-provision`.
   - `--port COMx`: VCOM port, needed only for flash-and-watch.
   - `--commander`, `--serialno` (J-Link) and `--timeout`.
2. **Expected S/N:** computed with the same algorithm and the host date, then printed.
3. **Configure,** in `cmake_gcc/`:

   ```sh
   cmake --preset project -B <build-dir> -DXBEE_APP_SELECT=PROVISION \
         -DSN_BURNER_SEQUENCE=NNNNN -DSN_BURNER_BUILD_YYWW=YYWW
   ```

   This build has its own directory, so the default CLI build is untouched.
4. **Build:** `cmake --build <build-dir> --config base --target xbee_provision`.
5. **Copy:** `<build-dir>/base/xbee_provision.hex` is copied to
   `<build-dir>/units/xbee_provision_<S/N>.hex`. This gives traceability and makes the wrong
   unit's image harder to pick.
6. **Prompt:** `Flash <file> and watch the log? [y/N]`. On "y":
   1. Open VCOM at 115200 first, so that no line is lost after the reset. pyserial is imported
      only at this point. If it is missing, the script prints the install command
      (`pip install -r tools/requirements.txt`).
   2. Flash with `commander flash <hex> --device EFM32PG28B210F1024IM68 [--serialno ...]`. This
      is not a mass erase, so the rest of the NVM3 region survives.
   3. Strip ANSI colour codes from each log line.
   4. Wait for `serial number written|not written`, or the timeout.
   5. Compare the written value with the expected S/N.
   6. Exit with 0 on a pass and non-zero otherwise.
7. **Commander lookup**, in the same order as `cmake_gcc/toolchain.cmake`: `--commander`, then
   `POST_BUILD_EXE`, then `~/.silabs/slt/installs/archive/*/commander(.exe)`, then `PATH`.

## Simplicity Studio changes required (PRIME RULE)

| Tool | Component / instance | Setting | Current value | Required value |
| --- | --- | --- | --- | --- |
| Software Components | NVM3 Default Instance (`nvm3_default`) | Installed | No | Yes |
| Software Components | NVM3 back-end for default instance in flash (`nvm3_default_flash_backend`) | Installed | No | Yes |
| NVM3 Default Instance configuration | `NVM3_DEFAULT_NVM_SIZE` | (new) | — | 40960 (default; must match WTX-FW) |
| NVM3 Default Instance configuration | `NVM3_DEFAULT_CACHE_SIZE` | (new) | — | 200 (default; matches WTX-FW) |
| NVM3 Default Instance configuration | `NVM3_DEFAULT_MAX_OBJECT_SIZE` | (new) | — | 254 (default; matches WTX-FW) |
| NVM3 Default Instance configuration | `NVM3_DEFAULT_REPACK_HEADROOM` | (new) | — | 0 (default; matches WTX-FW) |

NVM3 Core comes in as a dependency. After saving and regenerating, expect the following:

- `config/nvm3_default_config.h` exists.
- `autogen/sl_event_handler.c` calls `nvm3_initDefault()`.
- The `.nvm` section in `autogen/linkerfile.ld` is 40960 bytes.

## Resource impact

- **Flash:**
  - NVM3 core plus the new application code: a few kB, measured from the map file once built.
  - The NVM3 region (`0x080F4000` to `0x080FDFFF`, 40 kB) lies outside the image.
- **RAM:** the NVM3 object cache, 200 entries of about 8 B each, so about 1.6 kB, plus the
  instance handle. Measured from the map file.
- **First boot on a blank board:** NVM3 formats its 5 pages once.
- **Peripherals:** MSC through the NVM3 flash back-end. No new pins, interrupts or clocks are
  configured by the application.
- **Energy:** no change. NVM3 does not keep the device out of EM2.
- **Blocking:**
  - The writes stall the CPU while flash is programmed.
  - **Hex build:** the write runs after the XBee session has closed.
  - **CLI build:** the write runs after the operator has answered, with no request in flight.
  - The XBee UART receives through UARTDRV and LDMA either way.

## Risks and open questions

1. **The issue's example check digit is wrong.**
   - `2640-00123-4` does not satisfy ISO 7064 MOD 11-10. The correct `C` for `264000123` is
     **8**, so the S/N is `2640-00123-8`.
   - The same algorithm reproduces the standard's own example, `0794` -> 5.
   - Recommendation: correct the example in issue #4 and in the WTX-FW specification text. This
     work does not edit either.
2. **NVM3 region mismatch.** If the two images placed NVM3 differently, WTX-FW would not see the
   S/N, or could reformat the region. Mitigations:
   - identical configuration and identical FLASH length in both images;
   - compare `__nvm3Base` in both `.map` files.
3. **SDK version gap.** This project uses 2025.6.2 and WTX-FW uses 2025.12.3. NVM3 on-flash
   compatibility between them is assumed, and will only be proven on target.
4. **A mass or chip erase between provisioning and flashing WTX-FW wipes the S/N.** The production
   flow in `readme.md` must say that WTX-FW is flashed without a mass erase.
5. **Duplicate serial numbers if one hex is reused for several units.**
   - Mitigation: one image per unit, named by its S/N.
   - Allocating unique NNNNN values is the caller's job until the CI/CD release system takes it
     over.
6. **A provisioning image left on a board** provisions again and rewrites the same S/N on every
   boot. This is harmless, and NVM3 wear-levels the writes.
7. **`cmake --preset project -B <dir>`:** it is to be confirmed during implementation that the
   command-line `-B` overrides the preset's `binaryDir`.
8. **WTX-FW read code is not available locally yet.** Until it is, the end-to-end check uses
   Simplicity Commander's NVM3 read/parse instead of the LCD.

## Verification

1. **Host tests:**
   - Run: `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure`.
   - The new `test/test_device_sn.c` covers:
     - Check digit: `0794` -> 5, `264000123` -> 8, `264100123` -> 3.
     - ISO week: 2026-10-06 -> 2641; 2026-12-28 -> 2653; 2027-01-01 -> 2653; 2024-12-30 -> 2501;
       2021-01-03 -> 2053; 2020-12-31 -> 2053.
     - `__DATE__` parsing: `"Oct  6 2026"` is accepted and malformed strings are rejected.
     - Sequence parsing: wrong length and non-digits are rejected.
     - YYWW parsing: `2753`, `2600` and `2654` are rejected.
     - Formatting: exactly 13 bytes, NUL-terminated.
2. **Builds, with no warnings:**
   1. Default (CLI): `cd cmake_gcc && cmake --workflow --preset project`.
   2. `XBEE_APP_SELECT=PROVISION` without a sequence must stop at the `#error`.
   3. `python tools/provision.py --sequence 00123` builds the image and prints the expected S/N
      (`2641-00123-3` on 2026-10-06). Answer "N" at the flash prompt.
3. **Map check:** `__nvm3Base` is `0x080F4000`, the same as in WTX-FW.
4. **On target (only with the owner's go-ahead):**
   1. **CLI build:**
      - `show device serial-number` reports `not set`.
      - `device serial-number 00123`, answered N, writes nothing.
      - The same command answered y writes, and `show` reports the value.
      - A different NNNNN warns and asks before overwriting.
      - `week 2653` is applied.
      - Malformed input is rejected.
   2. **Hex build through the script's flash-and-watch:**
      - A passing XBee run logs `serial number written: ...`.
      - A forced XBee failure logs `serial number not written: ...`.
   3. **Hand-over to WTX-FW:** flash WTX-FW without a mass erase. Then:
      - Commander's NVM3 read/parse shows key `0x3` holding 13 bytes. The exact syntax will be
        checked with `commander nvm3 --help`.
      - Once WTX-FW has the read code, `Info > SN` on the LCD shows the value.
5. **History:** the history entry records which of these steps were actually run.

## Later work (not in this plan)

- The CI/CD release system allocates NNNNN and calls `tools/provision.py`, with a
  non-interactive option added at that point.
- Replace the `XBEE_APP_SELECT` macro-section input with the build preset from
  `feat/provision-build-preset`.
