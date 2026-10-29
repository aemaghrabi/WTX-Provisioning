# Device serial number in USERDATA, phase 3: not-set check

Date: 2026-10-29
Related plan: docs/plan/2026-10-29-sn-userdata-storage.md (Phase 3)
Previous phase: docs/history/2026-10-29-sn-userdata-phase2.md

## Summary

"Not set" is now decided exactly as WTX-FW decides it: the first byte of the serial number slot
(`0x0FE0008C`) reads `0xFF`. Anything else counts as set, well formed or not, and is shown and
judged by the caller. Writing still needs all 16 slot bytes erased. A slot that reads as not set
but holds other bytes is reported as such and is not written.

## Motivation

The contract says WTX-FW detects "never provisioned" only by byte 0 being `0xFF`; raw flash has
no length or existence metadata. Our console and hex build now report the same state WTX-FW
would see. This is topic 5 (Not-set check) of the owner's scope.

## Changes

| File | Change |
| --- | --- |
| `src/services/src/mfg_store.c` | `mfg_store_read_device_sn()` returns `SL_STATUS_NOT_FOUND` when the first byte is `0xFF`. Before, all 13 bytes had to be erased. The blank check used for writing is unchanged (all 16 bytes). |
| `src/services/inc/mfg_store.h` | Documents the byte-0 rule, and that "not set" is not the same as "writable". |
| `src/app/src/cli.c` | `device serial-number` on a not-set but non-blank slot: `% Serial number not set, but its USERDATA slot is not blank. Nothing written.` |
| `src/app/src/sn_burner.c` | Same case in the hex build: `serial number not written: it reads as not set, but its USERDATA slot is not blank`. |
| `src/app/src/cli_show.h` | Comment: the not-set rule, and the NUL-terminator flag from phase 2. |
| `test/test_mfg_store.c` | New `test_not_set_rule`: an erased first byte with data after it, or with only the last slot byte written, reads as not set, is not blank and is not written. `test_read_malformed`: only the first byte written reads as set and malformed (no terminator); a `0x00` first byte reads as set. |

## Simplicity Studio changes (made by the user)

None.

## Verification

Build and static review only. Nothing was run on target.

- **Default (CLI) firmware build:** succeeded with no warnings or errors.
- **PROVISION build:** `python tools/provision.py --family 18 --sequence 00123` built
  `xbee_provision_182644001232.hex` with no warnings. N was answered at the flash prompt.
- **Host tests:** not run, because there is no host C compiler on this machine. As a partial check,
  `test/test_mfg_store.c`, `test/fake_userdata_flash.c` and `src/services/src/mfg_store.c`
  passed `arm-none-eabi-gcc -std=c11 -Wall -Wextra -Werror -fsyntax-only`.

## Known limitations and follow-ups

- `show device serial-number` prints `not set` for any slot whose first byte is erased, as
  WTX-FW would. It does not say whether the rest of the slot is blank; the write command
  reports that case.
- **No erase step yet (phase 4).** A slot that is not blank can still only be cleared with
  `commander device pageerase --region @userdata`.
- **Not yet updated:** `readme.md` and the CLAUDE.md Project facts. The plan does that after
  phase 4.
