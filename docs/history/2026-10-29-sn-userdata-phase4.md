# Device serial number in USERDATA, phase 4: erase step

Date: 2026-10-29
Related plan: docs/plan/2026-10-29-sn-userdata-storage.md (Phase 4)
Previous phase: docs/history/2026-10-29-sn-userdata-phase3.md

## Summary

- **Console command.** A new privileged EXEC command, `erase device userdata`, erases the whole
  1 kB USERDATA page after a `[N/y]` question, the same effect as
  `commander device pageerase --region @userdata`.
- **Hex build.** The PROVISION image now erases the page before every write.
- **Write refusals.** The console's refusals now say to run the erase first.
- **Docs.** `readme.md` and the CLAUDE.md Project facts now describe the USERDATA storage.

## Motivation

Topic 6 (Erase step) of the owner's scope. The contract erases with Commander; the owner chose a
console command in this project's Cisco IOS style, a whole-page erase, and erase-first for every
hex-build run. Flash cannot be rewritten in place, so without an erase a serial number could not
be replaced from this firmware.

## Changes

| File | Change |
| --- | --- |
| `src/drivers/inc/userdata_flash.h`, `src/drivers/src/userdata_flash.c` | New `userdata_flash_erase()`: `MSC_Init()`, then `MSC_ErasePage(USERDATA_BASE)`, then `MSC_Deinit()`, then a check that the whole page reads `0xFF` (`SL_STATUS_FAIL` if not). New `userdata_flash_is_erased()`, a word-by-word check of the page. A static assert checks that `USERDATA_BASE` lies on a flash page boundary, which `MSC_ErasePage()` asserts. |
| `src/services/inc/mfg_store.h`, `src/services/src/mfg_store.c` | New `mfg_store_is_blank()` (whole page) and `mfg_store_erase()`. |
| `src/app/src/cli.c` | New tree `erase` -> `device` -> `userdata` (M_PRIV), placed alphabetically between `end` and `exit`. New `CLI_STATE_ERASE_CONFIRM`, answered like the serial number question: `?` help, Ctrl-C, line overflow and timeout all give "Not erased". The question names the stored serial number, or says none is set. An already blank page gives "USERDATA is already blank. Nothing erased." Every write refusal that needs an erase now adds `% Leave configuration mode and run "erase device userdata" first.` |
| `src/app/inc/cli_config.h` | New `CLI_ERASE_CONFIRM_TIMEOUT_MS`, 30000. |
| `src/app/src/sn_burner.c`, `src/app/inc/sn_burner.h` | `check_slot()` and its "same SN passes without writing" path are removed. `burn()` now logs what the page held (`erasing USERDATA (it held serial number X)`, `(blank)`, or `(it held no serial number, but was not blank)`), erases, then writes and verifies. A failed erase gives `serial number not written: erasing USERDATA failed, status 0x....`. The unused `<string.h>` include is removed. |
| `tools/provision.py` | Docstring: every run erases the whole USERDATA page. |
| `test/fake_userdata_flash.{c,h}`, `test/test_mfg_store.c` | The fake gains erase, erase-failure injection and an erase count. New tests: page blank check, erase then rewrite with a different SN, failed erase leaves the page unchanged. |
| `readme.md` | Serial number section: USERDATA storage, 13 bytes, the not-set rule, erase-first in the hex build, the console flow including `erase device userdata`. Production step 4: the serial number is in USERDATA, not NVM3. |
| `CLAUDE.md` | Project facts: new "Device serial number" entry. The NVM3 entry notes that key `0x00003` and `nvm_store` are no longer used and are marked for deletion. |

## Simplicity Studio changes (made by the user)

None.

## Verification

Build and static review only. Nothing was run on target.

- **Default (CLI) firmware build:** `cmake --workflow --preset project` succeeded with no
  warnings or errors.
- **PROVISION build:** `python tools/provision.py --family 18 --sequence 00123` built
  `xbee_provision_182644001232.hex` with no warnings. N was answered at the flash prompt.
- **Host tests:** not run, because there is no host C compiler on this machine. As a partial
  check, `test/test_mfg_store.c`, `test/fake_userdata_flash.c` and `src/services/src/mfg_store.c`
  passed `arm-none-eabi-gcc -std=c11 -Wall -Wextra -Werror -fsyntax-only`.
- **Plan risk 3 (erase aborted by an interrupt), checked statically:**
  - `MSC_WRITECTRL.IRQERASEABORT` resets to 0: reference manual, `MSC_WRITECTRL` register table
    (`docs/manuals/efm32pg28_reference_manual.md` line 5299), and
    `_MSC_WRITECTRL_IRQERASEABORT_DEFAULT` in `efm32pg28_msc.h`.
  - Neither the SDK's emlib nor NVM3 sets it.
  - So an interrupt does not abort the erase. The prose in section 5.3.12 ("by default ... the
    erase is aborted") contradicts the register table; the `ERASEABORTED` description agrees with
    the table.
  - The blank check after the erase covers it either way.
- **Still to run on target**, with the owner's go-ahead:
  1. `erase device userdata`, then read the 1 kB page; every byte should be `0xFF`.
  2. `device serial-number 18 00123`, then `show device serial-number`.
  3. Read 16 bytes at `0x0FE0008C`.
  4. Try a second `device serial-number` with another SN; it should be refused with the erase hint.
  5. A hex flash-and-watch, run twice on the same unit; both runs should pass.

## Known limitations and follow-ups

- **Whole-page erase (plan risk 2).** Every hex-build run, and every `erase device userdata`,
  wipes all of USERDATA. That includes any SDK manufacturing tokens and, once Part 2 exists, the
  variant bytes at `0x110`/`0x111`. Part 2 has to revisit this.
- **Erase time not documented here.** The page erase time comes from the device datasheet, which
  is not in `docs/manuals/`. The CPU stalls on flash for that time.
- **Flash endurance.** Each hex-build run costs one erase cycle of the USERDATA page.
- **Production step 4** in `readme.md` keeps "without a mass or chip erase". The serial number no
  longer needs that, because USERDATA is not part of a mass erase, but NVM3 still does. Whether
  the rule stays is for the owner to decide.
- **Recommended Studio change (not made):** install the `emlib_msc` component explicitly. Today
  it is only pulled in by NVM3, which is marked for later removal.
