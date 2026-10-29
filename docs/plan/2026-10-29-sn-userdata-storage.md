# Plan: move the device serial number to USERDATA (contract Part 1, topics 1, 2, 5, 6)

Status: In progress (Phase 1 done, build-verified only; phases 2 to 4 not started)
Date: 2026-10-29
Related history: docs/history/2026-10-29-sn-userdata-phaseN.md (one per phase, written as each phase is done)
Supersedes (storage only): docs/plan/2026-10-29-serial-number-family-code.md

## Context

The factory provisioning contract says WTX-FW reads the Device SN from the USERDATA page at
`0x0FE0008C` (the SDK's `TOKEN_MFG_SERIAL_NUMBER` slot): 13 bytes (12 digits + NUL), with "not
set" meaning byte 0 is `0xFF`. This project writes it to NVM3 key `0x00003` as 12 bytes with
no NUL, so a WTX-FW built to the contract would show "NOT SET" on every unit we provision.

This plan covers four rows of the comparison table: 1 Storage, 2 Bytes stored, 5 Not-set check,
6 Erase step. The erase step is a console command, not Commander. Unchanged: the firmware on the
target still writes the SN (row 3), still only after XBee provisioning passes (row 4), and still
reads it back to check (row 7). Part 2 (variant bytes) is out of scope.

### Decisions by the project owner (2026-10-29)

| Topic | Decision |
| --- | --- |
| Scope | Comparison-table rows 1, 2, 5, 6. Firmware keeps writing. |
| Erase command | `erase device userdata`, privileged EXEC (like IOS `erase startup-config`), asks `[N/y]` |
| Erase scope | The whole 1 kB USERDATA page, the same as `commander device pageerase --region @userdata` |
| Hex build | `sn_burner` always erases the page first, then writes |
| `nvm_store` | Kept, no longer used for the SN, and marked for deletion later. Not removed now. |

### Verified facts the design relies on

- `TOKEN_MFG_SERIAL_NUMBER` = `SL_TOKEN_STATIC_DEVICE_TOKENS | 0x08C`, 16 bytes. Static device
  tokens map to `USERDATA_BASE` on Series 2.
  - Source: SDK `platform/service/token_manager/inc/sl_token_manager_manufacturing.h`.
- Device header `efm32pg28b210f1024im68.h`:
  - `USERDATA_BASE` = `0x0FE00000`, `USERDATA_SIZE` = `0x400` (1 kB, which matches RM section 5.3).
  - `FLASH_PAGE_SIZE` = `0x2000`, so USERDATA_BASE is aligned to it, as `MSC_ErasePage()` asserts.
  - `_SILICON_LABS_32B_SERIES_2_CONFIG` = 8.
- **The application firmware can write USERDATA with the ordinary MSC; the SE is not needed.**
  - The SDK's own token manager (`token_manager/legacy/src/sl_token_manufacturing.c`) goes
    through `sl_se_write_user_data()` only for Series 2 config 1 (xG21). For config 8 it uses
    `hal_flash_write()`, which is `MSC_WriteWord()`.
  - RM section 5.3.4: the UD page "can be erased and written by software when
    MISCLOCKWORD.UDLOCKBIT is 0".
  - The contract's claim that USERDATA writes are SE-gated is wrong for this part.
- emlib `em_msc.c` is already in the build, as an NVM3 dependency (`cmake_gcc/xbee_provision.cmake`).
  - Its erase and write functions run from RAM (`MSC_RAMFUNC`).
  - It checks `LOCKED`/`INVADDR` and returns `MSC_Status_TypeDef`.
  - There is no `sl_hal_msc` in this SDK, so emlib MSC is the SDK API to use here.
- Flash rules (RM section 5.3.12):
  - A bit only goes from 1 to 0, and an erase sets every bit back to 1.
  - Each 64-bit double-word can be written at most twice per erase.
  - Writes are whole words: the address must be word-aligned and the length a multiple of 4.
  - The slot offset `0x08C` is word-aligned.

## Architecture (new modules, CLAUDE.md layering)

- **`src/drivers/inc/userdata_flash.h`, `src/drivers/src/userdata_flash.c`**: a board-level
  wrapper over emlib MSC for the USERDATA page only.
  - `userdata_flash_read(offset, out, len)`: a bounds-checked copy from `USERDATA_BASE + offset`,
    a plain memory read.
  - `userdata_flash_write(offset, data, len)`:
    - Requires a word-aligned offset, `len % 4 == 0`, and `offset + len <= USERDATA_SIZE`.
    - Runs `MSC_Init()`, then `MSC_WriteWord()`, then `MSC_Deinit()`.
  - `userdata_flash_erase(void)`: `MSC_ErasePage((uint32_t *)USERDATA_BASE)`, then a check
    that all 1024 bytes read `0xFF`.
  - `MSC_Status_TypeDef` maps to `sl_status_t`:
    - `mscReturnLocked` gives `SL_STATUS_FLASH_PROGRAM_FAILED`, with a log line that names
      UDLOCK.
    - A timeout gives `SL_STATUS_TIMEOUT`.
    - An invalid address gives `SL_STATUS_INVALID_RANGE`.
  - All of these are synchronous one-time factory steps, as `nvm_store_write_device_sn()` is
    today, and are documented the same way.
- **`src/services/inc/mfg_store.h`, `src/services/src/mfg_store.c`**: manufacturing data in
  USERDATA.
  - Defines `MFG_STORE_SN_OFFSET 0x08CU` (equal to `TOKEN_MFG_SERIAL_NUMBER & 0x0FFF`) and
    `MFG_STORE_SN_SLOT_SIZE 16U`.
  - API: `mfg_store_read_device_sn(out, cap)`, `mfg_store_write_device_sn(sn)`,
    `mfg_store_device_sn_slot_is_blank(bool *blank)`, `mfg_store_erase(void)`.
  - Reuses `device_sn_validate()` and `device_sn_to_printable()` (`src/utils/src/device_sn.c`)
    without changes.
- **Build:** both `.c` files are listed in `cmake_gcc/CMakeLists.txt` under
  `# Add additional sources here`. The include paths already exist.
- **Host test:** `test/test_mfg_store.c` with `test/fake_userdata_flash.c`.
  - The fake is a 1 kB RAM array that behaves like flash: erase sets every byte to `0xFF`, and
    a write ANDs into the array.
  - Registered in `test/CMakeLists.txt` with `add_module_test`, the pattern already used there.

## Phase 1: Storage (topic 1)

Goal: the SN lives at `0x0FE0008C`. Nothing writes the NVM3 key any more. The format stays as
it is: 12 bytes, no NUL.

- Add `userdata_flash` and `mfg_store` as described above.
  - As implemented: `userdata_flash_erase()` and `mfg_store_erase()` come with Phase 4, which
    is their only user.
  - `mfg_store` does not log, so it builds for the host tests; its callers log.
- **Interim rules for this phase only** (phases 2 and 3 replace them):
  - Write exactly 12 bytes.
  - "Not set" means all 12 bytes are `0xFF`.
  - Write only when the 16-byte slot is blank. Otherwise refuse with `SL_STATUS_ALREADY_EXISTS`.
- Write path:
  1. Validate with `device_sn_validate()`.
  2. Check that the slot is blank.
  3. `userdata_flash_write()`.
  4. Read back and `memcmp` (row 7 is kept).
- **`src/app/src/cli.c`:**
  - `cmd_device_sn_set()` / `handle_sn_confirm()` call `mfg_store` instead of `nvm_store`.
  - The overwrite question is removed, together with `sn_overwrite`, `stored_sn_shown` and the
    overwrite branch of `print_sn_question()`, because flash cannot be overwritten in place.
  - The same SN already stored gives "already stored, nothing written", as today.
  - A different SN gives `% Serial number X is already stored. Nothing written.`
  - Help and question text changes from "MCU NVM" to "USERDATA".
- **`src/app/src/cli_show.c`:** `cli_show_device_sn()` reads through `mfg_store`. The
  NVM3-specific cases (`INVALID_TYPE` / `INVALID_COUNT`) are removed.
- **`src/app/src/sn_burner.c`:** `report_stored()` and `burn()` go through `mfg_store`. The log
  line formats stay unchanged, because `tools/provision.py` matches `serial number written:` and
  `serial number not written:`.
- **`nvm_store` stays in the build, unused.** Its file header gets a note: "No longer used for
  the device serial number (moved to USERDATA, see docs/plan/2026-10-29-sn-userdata-storage.md).
  Kept for now; to be deleted later."
- Doc comments: `device_sn.h` (storage paragraph), `sn_burner.h`, and the `provision.py`
  docstring and argparse description (NVM3 becomes USERDATA, and the remark about keeping the
  rest of NVM3 is reworded).

## Phase 2: 13 stored bytes (topic 2)

Goal: the stored form is the 12 digits followed by `0x00`, exactly 13 bytes, as WTX-FW's
`DEVICE_SN_STR_LEN` expects.

- **`device_sn.h`:**
  - Add `DEVICE_SN_STORED_LEN 13U`: the digits plus the NUL.
  - `DEVICE_SN_LEN` (12) stays as the digit count.
  - The comments that say "no NUL terminator" are updated.
- **Write:** a 16-byte slot image of the 12 digits, `0x00`, then 3 bytes of `0xFF`.
  - `0xFF` follows the contract's "only write the first 13 and leave the rest alone": writing
    `0xFF` does not change erased flash.
  - The image is 16 bytes because MSC writes whole words.
  - The read-back compares all 13 bytes.
- **Read:** byte 12 must be `0x00`. Otherwise it is reported as "stored, but not terminated"
  (`SL_STATUS_INVALID_COUNT`). `show device serial-number` prints the sanitised digits with
  "(no NUL terminator)".
- Tests:
  - `test_mfg_store`: the 13-byte image, the padding left as `0xFF`, and the missing-terminator
    case.
  - `test_device_sn`: unchanged.

## Phase 3: Not-set check (topic 5)

Goal: report "not set" exactly as WTX-FW decides it.

- **`mfg_store_read_device_sn()`:**
  - Byte 0 is `0xFF`: `SL_STATUS_NOT_FOUND` ("not set").
  - Anything else: "set". The 13 bytes are returned as they are, and the caller checks the form,
    the check digit and the terminator for display, as `cli_show.c` already does with
    `device_sn_validate()`.
- **Writing needs the whole 16-byte slot blank. That is not the same as "not set".** A slot with
  byte 0 at `0xFF` but other bytes written reads as "not set" and still cannot be written.
  - The CLI then says: `% Serial number not set, but its USERDATA slot is not blank. Nothing written.`
  - `sn_burner` logs a `serial number not written:` reason.
- Tests: byte 0 `0xFF` with garbage after it gives "not set" and "not blank"; byte 0 written
  with the rest erased gives "set, malformed".

## Phase 4: Erase step (topic 6), console style

Goal: an operator can erase USERDATA from the console, and the hex build erases before every
write.

- **CLI tree** (`cli.c`):
  - New `root` entry `erase` (M_PRIV), placed alphabetically between `end` and `exit`.
  - Under it, `device` and then `userdata` (`CMD_ERASE_USERDATA`).
  - Update `ROOT_COUNT` and the hand-kept child counts.
- **Flow:**
  - Already blank: `USERDATA is already blank. Nothing erased.`
  - Otherwise:
    - It prints `Erase the whole USERDATA page (serial number X)? Everything stored there is lost. [N/y] `.
    - It then enters a new `CLI_STATE_ERASE_CONFIRM`.
  - Answering:
    - The state uses the timeout `CLI_ERASE_CONFIRM_TIMEOUT_MS` in `src/app/inc/cli_config.h`.
    - The answer is read with `equals_ignore_case`, as in `handle_sn_confirm()`.
    - `?` help comes from `show_answer_help()`.
    - Ctrl-C and the timeout are handled the same way as `CLI_STATE_SN_CONFIRM`.
  - "y" runs `mfg_store_erase()`, which verifies the page is all `0xFF`, then prints
    `[OK] USERDATA erased` or `% Erasing USERDATA failed, status 0x....`.
- **CLI write refusal** from phases 1 and 3 gains the hint:
  `Run "erase device userdata" in privileged mode first.`
- **Hex build** (`sn_burner.c` `burn()`): once XBee provisioning has passed, it erases the page
  and then writes and verifies.
  - Before erasing it logs `erasing USERDATA (it held serial number X)`, or `(blank)`.
  - A failed erase gives `serial number not written: erasing USERDATA failed, status 0x....`.
  - `provision.py`: only the docstring changes, to say that every run erases the whole USERDATA
    page.

## Simplicity Studio changes (PRIME RULE)

- **Required: none.** `em_msc.c` and `em_msc.h` are already in the build.
- **Recommended, not required:** in Software Components, install the `emlib_msc` (MSC)
  component explicitly. Today it is only pulled in by NVM3, and the plan marks NVM3 code for
  later removal. Without the explicit component, removing the NVM3 components would silently
  drop MSC. I would only ask for this, never do it.

## Resource impact

- **Flash:** about 1 to 1.5 kB for two small modules and one CLI command.
- **RAM:** a 16-byte stack image, and no page buffer (the whole page is erased).
- **Peripherals:** the MSC clock is enabled only during an operation (`MSC_Init()`/`MSC_Deinit()`).
- **Energy modes:** no change.
- **CPU stall:** an erase or write blocks the super loop. The CPU stalls on flash while the MSC
  routines run from RAM. UARTDRV LDMA keeps receiving.

## Risks and open items

1. **WTX-FW coordination.** After phase 1, new units have no NVM3 SN. A WTX-FW that still reads
   NVM3 shows "not set" until it moves to the contract's USERDATA read.
2. **Whole-page erase.** Every hex-build run, and every `erase device userdata`, wipes all of
   USERDATA. That includes any SDK manufacturing tokens and, later, the Part 2 variant bytes at
   `0x110`/`0x111`. Part 2 will have to revisit this, either by writing the variant bytes in the
   same pass or by preserving the page.
3. **Erase aborted by an interrupt.** RM section 5.3.12 says an erase is aborted by any interrupt
   unless `IRQERASEABORT` is cleared. During implementation, check whether `em_msc` /
   `MSC_ErasePage` handle this. The blank check after the erase always catches an aborted erase.
   If aborts happen, the fix (for example, clearing `IRQERASEABORT` through an emlib API) is
   proposed before it is applied.
4. **UDLOCK.** If `MSC_MISCLOCKWORD.UDLOCKBIT` is set on a unit, every write and erase fails
   with "locked". This is reported, not worked around.
5. **Units already provisioned in NVM3** keep their stale NVM3 object, and it is ignored.
   Deleting it is part of the later `nvm_store` removal.
6. **The contract's SE-gating text and example SN are wrong** (see Verified facts above, and the
   earlier review). The contract is not edited here.

## Verification (each phase, before its history entry)

1. **Firmware build:** `cd cmake_gcc && cmake --workflow --preset project`, with no warnings.
2. **Host tests:**
   `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure`,
   including the new `test_mfg_store`. If no host compiler is available, the history entry says
   so.
3. **Hex build:** `python tools/provision.py --family 18 --sequence 00123` builds, prints the
   expected SN, and I answer N at the flash prompt.
4. **On target, only with the owner's go-ahead:**
   - CLI: `device serial-number 18 00123`, then `show device serial-number`.
   - Read the slot back with the J-Link MCP `read_memory` at `0x0FE0008C`, 16 bytes, or with
     `commander readmem --region @userdata`.
   - Phase 4: `erase device userdata`, then confirm the page reads all `0xFF`.
   - Hex flash-and-watch.
5. **Docs for each phase:**
   - A history entry `docs/history/2026-10-29-sn-userdata-phaseN.md`.
   - Plan status updated as the phases complete.
   - At the end: CLAUDE.md Project facts (SN in USERDATA `0x0FE0008C`, 13 bytes; `nvm_store`
     kept and marked for deletion), and the `readme.md` serial-number section and new command.

## Files touched

- **New:**
  - `src/drivers/{inc,src}/userdata_flash.*`
  - `src/services/{inc,src}/mfg_store.*`
  - `test/test_mfg_store.c`, `test/fake_userdata_flash.c`
  - `docs/plan/2026-10-29-sn-userdata-storage.md`
  - history entries
- **Changed:**
  - `src/app/src/cli.c`, `src/app/src/cli_show.c`, `src/app/src/sn_burner.c`
  - `src/app/inc/cli_config.h`, `src/app/inc/sn_burner.h`
  - `src/utils/inc/device_sn.h`, `src/services/inc/nvm_store.h` (deletion note only)
  - `cmake_gcc/CMakeLists.txt` (`# Add additional sources here` only)
  - `test/CMakeLists.txt`, `tools/provision.py`, `readme.md`, `CLAUDE.md`
- **Not touched:** anything Studio-owned (`config/`, `autogen/`, `.slcp`, `.pintool`, generated
  CMake files).
