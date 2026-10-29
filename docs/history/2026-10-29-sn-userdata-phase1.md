# Device serial number in USERDATA, phase 1: storage

Date: 2026-10-29
Related plan: docs/plan/2026-10-29-sn-userdata-storage.md (Phase 1)

## Summary

The device serial number is now stored in the USERDATA flash page at `0x0FE0008C`, the SDK's
`TOKEN_MFG_SERIAL_NUMBER` slot (16 bytes), instead of NVM3 key `0x00003`. Both builds, the CLI
console and the PROVISION hex image, read and write it there. The stored form is unchanged: 12
digits `FFYYWWSSSSSC`, no NUL. Phases 2 to 4 change the stored form, the "not set" rule, and
add the erase step.

## Motivation

The factory provisioning contract says WTX-FW reads the serial number from USERDATA at
`0x0FE0008C`. USERDATA is not part of a main flash mass erase (efm32pg28 reference manual,
5.3.12). The owner decided to implement topic 1 of the contract comparison (Storage) as this
plan's first phase.

## Changes

| File | Change |
| --- | --- |
| `src/drivers/inc/userdata_flash.h`, `src/drivers/src/userdata_flash.c` | New. USERDATA page read (plain memory read) and word write through emlib `MSC_Init()`/`MSC_WriteWord()`/`MSC_Deinit()`. Arguments are bounds- and alignment-checked; the source is copied to an aligned buffer first. `MSC_Status_TypeDef` is mapped to `sl_status_t`, and a locked page is logged. |
| `src/services/inc/mfg_store.h`, `src/services/src/mfg_store.c` | New. SN slot at offset `0x08C`, 16 bytes. Read: "not set" when all 12 bytes read `0xFF` (interim rule). Write: validate, refuse unless all 16 slot bytes are erased (`SL_STATUS_ALREADY_EXISTS`), program 12 bytes, read back and compare. No logging, so the module builds for the host tests. |
| `src/app/src/cli.c` | `device serial-number` uses `mfg_store`. The overwrite question and its state (`sn_overwrite`, `stored_sn_shown`) are removed, because flash cannot be rewritten in place. A stored SN, or a slot that is not blank, now ends the command with "Nothing written". Text says "USERDATA" instead of "MCU NVM". |
| `src/app/src/cli_show.c`, `src/app/src/cli_show.h` | `show device serial-number` reads through `mfg_store`. The NVM3-only cases (wrong object length, counter object) are removed. |
| `src/app/src/sn_burner.c`, `src/app/inc/sn_burner.h` | Hex build writes through `mfg_store`. New `check_slot()`: an erased slot is written; the same SN already stored passes without writing; anything else ends with `serial number not written: <reason>`. The result line formats `provision.py` matches are unchanged. |
| `src/services/inc/nvm_store.h` | Note only: no longer used for the SN, kept for now, to be deleted later (owner's decision). The module stays in the build. |
| `src/utils/inc/device_sn.h` | Comments: storage is USERDATA, not NVM3. |
| `cmake_gcc/CMakeLists.txt` | `userdata_flash.c` and `mfg_store.c` added under `# Add additional sources here`. |
| `test/fake_userdata_flash.{c,h}`, `test/test_mfg_store.c`, `test/CMakeLists.txt` | New host test of `mfg_store` over a RAM page that behaves like flash (starts erased, writes only clear bits). |
| `tools/provision.py` | Docstring and argparse description: USERDATA instead of NVM3, and a slot that already holds a different SN reports "not written". |

## Simplicity Studio changes (made by the user)

None. `em_msc.c`/`em_msc.h` were already in the build as an NVM3 dependency.

## Verification

Build and static review only. Nothing was run on target.

- **Default (CLI) firmware build:** `cmake --workflow --preset project` with the cmake from
  `cmake_gcc/build/CMakeCache.txt` (cmake is not on PATH). It succeeded with no warnings or
  errors. The map file shows `userdata_flash_write` and `mfg_store_write_device_sn` linked.
- **PROVISION build:** `python tools/provision.py --family 18 --sequence 00123 --cmake <that cmake>`,
  answered N at the flash prompt. It built `xbee_provision_182644001232.hex` with no warnings,
  and its define check passed.
- **Host tests:** not run. There is no host C compiler on this machine (no gcc, cc or clang on
  PATH). As a partial check, `test/test_mfg_store.c`, `test/fake_userdata_flash.c` and
  `src/services/src/mfg_store.c` were compiled with `arm-none-eabi-gcc -std=c11 -Wall -Wextra
  -Werror -fsyntax-only`, with no diagnostics.
- **Still to run on target**, with the owner's go-ahead:
  - `device serial-number 18 00123`, then `show device serial-number`.
  - Read 16 bytes at `0x0FE0008C`, through the J-Link MCP or
    `commander readmem --region @userdata`.
  - A hex flash-and-watch.

## Known limitations and follow-ups

- **Interim rules until phases 2 and 3:**
  - 12 bytes are stored with no NUL, where the contract asks for 13.
  - "Not set" means all 12 bytes are erased, where the contract checks byte 0 only.
- **No erase yet (phase 4).** A unit whose slot holds a different SN cannot be re-provisioned
  from this firmware. Until then, the slot can only be cleared with
  `commander device pageerase --region @userdata`.
- **Hex re-run with the same SN:** nothing is programmed, but the step still logs
  `serial number written: <SN>` so `provision.py` reports PASS. A line before it says the SN
  was already stored. Phase 4 replaces this with erase-then-write.
- **WTX-FW coordination:** units provisioned from now on have no NVM3 serial number. A WTX-FW
  that still reads NVM3 key `0x00003` shows it as not set.
- **Not yet updated:** `readme.md` and the CLAUDE.md Project facts still describe NVM3 storage.
  The plan updates them at the end of phase 4.
