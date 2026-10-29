# Device serial number in USERDATA, phase 2: 13 stored bytes

Date: 2026-10-29
Related plan: docs/plan/2026-10-29-sn-userdata-storage.md (Phase 2)
Previous phase: docs/history/2026-10-29-sn-userdata-phase1.md

## Summary

The serial number is now stored as exactly 13 bytes at `0x0FE0008C`: the 12 digits
`FFYYWWSSSSSC` followed by one `0x00`. This is what the contract says WTX-FW reads (its
`DEVICE_SN_STR_LEN`). The write programs a 16-byte image: those 13 bytes, then 3 bytes of `0xFF`,
which leave the rest of the slot erased. A stored value without its NUL is reported, not
accepted.

## Motivation

The contract's "Exact bytes to write" requires exactly 13 bytes every time. WTX-FW reads 13
bytes and has no length metadata, so a missing terminator would make it display or transmit
whatever follows the digits. This is topic 2 (Bytes stored) of the owner's scope.

## Changes

| File | Change |
| --- | --- |
| `src/utils/inc/device_sn.h` | New `DEVICE_SN_STORED_LEN` (`DEVICE_SN_LEN + 1U`, 13). `DEVICE_SN_LEN` is now documented as the digit count. The header and `device_sn_format()` comments describe the 13-byte stored form. No code change in `device_sn.c`. |
| `src/services/src/mfg_store.c`, `src/services/inc/mfg_store.h` | Write: the image is the digits, `0x00`, then `0xFF` padding up to whole words (16 bytes), and the read-back compares all 13 bytes. Read: reads 13 bytes; when byte 12 is not `0x00`, it returns the digits (terminated in RAM) with `SL_STATUS_INVALID_COUNT`. "Not set" now means all 13 bytes are erased (interim rule until phase 3). |
| `src/app/src/cli.c` | `device serial-number`: a stored value without its NUL gives `% Serial number X is stored without its NUL terminator. Nothing written.` |
| `src/app/src/cli_show.c` | `show device serial-number`: such a value prints as `Serial number  X (no NUL terminator)`. |
| `src/app/src/sn_burner.c` | Hex build: such a value gives `serial number not written: USERDATA holds serial number X without its NUL terminator`. |
| `test/test_mfg_store.c` | Checks the 13-byte layout, that the padding stays erased, and the missing-terminator cases (terminator left erased, or another value there). Existing malformed-read cases are updated to the 13-byte rule. |
| `test/test_device_sn.c` | Comment only: the 12-byte `no_nul` case is about validation reading the digits only, no longer "as stored in NVM3". |

## Simplicity Studio changes (made by the user)

None.

## Verification

Build and static review only. Nothing was run on target.

- **Default (CLI) firmware build:** `cmake --workflow --preset project` succeeded with no
  warnings or errors.
- **PROVISION build:** `python tools/provision.py --family 18 --sequence 00123` built
  `xbee_provision_182644001232.hex` with no warnings. N was answered at the flash prompt.
- **Host tests:** not run, because there is still no host C compiler on this machine. As a
  partial check, `test/test_mfg_store.c`, `test/fake_userdata_flash.c`, `test/test_device_sn.c`
  and `src/services/src/mfg_store.c` were compiled with
  `arm-none-eabi-gcc -std=c11 -Wall -Wextra -Werror -fsyntax-only`, with no diagnostics.
- **Still to run on target:** write a serial number, then read 16 bytes at `0x0FE0008C`. Expected:
  12 ASCII digits, `00`, `FF FF FF`.

## Known limitations and follow-ups

- **Padding choice.** The 3 bytes after the NUL are left erased (`0xFF`), following the
  contract's "only write the first 13 and leave the rest alone". The contract also allows
  `0x00` padding. WTX-FW reads only the first 13 bytes, so either works.
- **Interim "not set" rule.** Until phase 3, "not set" means all 13 bytes are erased. The
  contract checks byte 0 only.
- **Units written during phase 1** hold 12 digits with no NUL. They now read as "no NUL
  terminator", and cannot be rewritten until phase 4 adds the erase step.
- **Not yet updated:** `readme.md` and the CLAUDE.md Project facts. The plan does that after
  phase 4.
