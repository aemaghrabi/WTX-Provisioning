# 2026-10-29: Serial number family code (FF), format FFYYWWSSSSSC

Plan: `docs/plan/2026-10-29-serial-number-family-code.md`

## Summary

The device serial number changed from `YYWW-NNNNN-C`, stored as 13 bytes with a NUL, to
`FFYYWWSSSSSC`:

- 12 decimal digits, with no separators and no NUL.
- Stored as exactly 12 bytes at NVM3 key `0x00003`.
- `FF` is a two-digit product family code. Both builds now require it as an input.
- The check digit covers all 11 preceding digits, `FF` included.

## Motivation

The project owner supplied the new format specification and set three things:

- `FF` is required input to both builds, because no value has been assigned yet.
- Storage stays in NVM3. Moving it to USERDATA is planned separately.
- The value is stored as 12 bytes with no terminator.

## Changes

- `src/utils/inc/device_sn.h`, `src/utils/src/device_sn.c`:
  - New constants `DEVICE_SN_LEN` (12) and `DEVICE_SN_STR_SIZE` (13, the RAM string size), plus
    `DEVICE_SN_FAMILY_DIGITS` and `DEVICE_SN_FAMILY_MAX`. `DEVICE_SN_STR_LEN` and
    `DEVICE_SN_TEXT_LEN` were removed.
  - New `device_sn_parse_family()`.
  - `device_sn_format()` takes the family code. Before it writes the output, it recomputes the
    check digit and runs `device_sn_validate()` on the result.
  - `device_sn_validate()` checks 12 digits and the check digit.
- `src/services/inc/nvm_store.h`, `src/services/src/nvm_store.c`:
  - Read and write exactly 12 bytes.
  - The read adds a NUL in RAM only.
- `src/app/src/sn_burner.c`, `src/app/inc/sn_burner.h`: new build input `SN_BURNER_FAMILY`,
  stringified, with a `_Static_assert` on its length and a check when it runs.
- `app.c`: `#error` when the PROVISION build has no `SN_BURNER_FAMILY`.
- `src/app/src/cli.c`:
  - The command is now `device serial-number <FF> <SSSSS> [week <YYWW>]`.
  - A new `<FF>` node was added, and `<NNNNN>` was renamed `<SSSSS>`.
  - Buffers resized.
- `src/app/src/cli_show.c`: buffers resized and messages updated for the new form.
- `cmake_gcc/CMakeLists.txt`: `SN_BURNER_FAMILY` added to both macro blocks.
- `tools/provision.py`:
  - New required `--family` argument, passed as `-DSN_BURNER_FAMILY`.
  - The script checks its own expected serial number before building.
  - The result regex now matches 12 digits.
  - `compile_commands.json` is checked for the family define.
- `test/test_device_sn.c`: new 11-digit check-digit vectors, family parsing tests, and updated
  format, validate and printable tests. These include rejecting the old `YYWW-NNNNN-C` form and the
  specification's example.
- `readme.md`, `CLAUDE.md` (Project facts, NVM3 key line), and the plan status of
  `docs/plan/2026-10-06-device-serial-number.md` (now Superseded, for the format only).

## Studio changes

None.

## Verification

- **CLI build:** `cmake --workflow --preset project` built with no warnings.
- **Provisioning build:** `python tools/provision.py --family 18 --sequence 00123` built it with
  no warnings.
  - It printed `182644001232` for 2026-10-29, ISO week 2644.
  - It stopped at the flash prompt, so nothing was flashed.
- **Build checks:**
  - With the PROVISION variant configured without `SN_BURNER_FAMILY`, the build stops at the new
    `#error`.
  - `--family` is required, and `--family 1` is rejected.
- **Check-digit vectors:** the vectors in `test/test_device_sn.c` were computed with
  `provision.py`'s `check_digit()`, which is an independent implementation. That function also
  reproduces the standard's example, `0794` -> 5. The specification's example `18072600123` gives
  0, not 4.
- **Host unit tests:** not run, because this machine has no host C compiler.
  `test/test_device_sn.c` and `device_sn.c` pass ARM GCC `-std=c99 -Wall -Wextra -fsyntax-only`.
  That checks syntax only; no test was executed.
- **On target:** not tested.

## Known limitations and follow-ups

- **WTX-FW:** it still expects 13 bytes, so it reports the new 12-byte object as not set until its
  `DEVICE_SN_STR_LEN` and its read code are changed. That work is outside this repository.
- **Units already burned in the old format** read back as wrong length (not set) until they are
  burned again.
- **`FF`:** no value is assigned yet. Record it in the plan and in `readme.md` once it is.
- **The specification text** still shows the wrong example check digit, and still calls the
  algorithm "ISBN-13-style".
- **Pending verification:** the host tests and the on-target test still have to be run.
