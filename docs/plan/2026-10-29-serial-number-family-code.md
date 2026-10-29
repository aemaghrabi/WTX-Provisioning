# Device serial number: add the FF product family code

Status: In progress (implemented; host tests and on-target verification not yet run)
Date: 2026-10-29
Supersedes (format only): docs/plan/2026-10-06-device-serial-number.md
Related history: docs/history/2026-10-29-serial-number-family-code.md

## Goal

Change the device serial number from `YYWW-NNNNN-C` to `FFYYWWSSSSSC`:

| Part | Digits | Meaning |
| --- | --- | --- |
| `FF` | 2 | Product family code. No value has been assigned yet, so it is a required input. |
| `YY` | 2 | Year of manufacture: the ISO 8601 week-numbering year, mod 100 (unchanged). |
| `WW` | 2 | ISO week of manufacture (unchanged). |
| `SSSSS` | 5 | Sequence number, 00000 to 99999 (renamed from `NNNNN`). |
| `C` | 1 | ISO 7064 MOD 11-10 check digit over all 11 preceding digits `FFYYWWSSSSS`. |

The new form has 12 digits. It has no dashes and no letters.

### Decisions by the project owner

| Topic | Decision |
| --- | --- |
| Source of `FF` | A required input in both builds. Hex build: `tools/provision.py --family FF`, which passes `-DSN_BURNER_FAMILY=FF`; without it the build stops at an `#error`. CLI build: `device serial-number <FF> <SSSSS> [week <YYWW>]`. |
| Storage | Unchanged: an NVM3 data object at key `0x00003`. Moving it to USERDATA is later, separate work. |
| Stored length | Exactly 12 bytes, with no NUL terminator. |

Measurable outcome:

- **CLI build:** `device serial-number 18 00123` stores 12 digits at key `0x00003` once the
  operator confirms, and `show device serial-number` reads them back.
- **Hex build:** `tools/provision.py --family 18 --sequence 00123` builds an image that logs
  `serial number written: <12 digits>`.

## Background and references

- The current design is in `docs/plan/2026-10-06-device-serial-number.md`, and this plan keeps
  its layering:
  - `device_sn` (utils)
  - `nvm_store` (services)
  - `sn_burner`, `cli`, `cli_show` (app)
  - `tools/provision.py` on the host
- `device_sn_check_digit()` (`src/utils/src/device_sn.c`) already takes a digit string of any
  length, so the algorithm does not change.
- `tools/provision.py` `check_digit()` is an independent Python implementation of the same
  algorithm. It gives `0794` -> 5, the standard's example, and `18072600123` -> 0.
- WTX-FW reads the value. Today it accepts exactly 13 bytes (`DEVICE_SN_STR_LEN` in
  `lcd_menu.h`).

## Design

### utils: `device_sn`

- **Constants:**
  - `DEVICE_SN_LEN` = 12U: the bytes stored, with no NUL. It must equal the new value in WTX-FW.
  - `DEVICE_SN_STR_SIZE` = `DEVICE_SN_LEN + 1U`: a RAM buffer that holds the number as a
    printable C string.
  - `DEVICE_SN_FAMILY_DIGITS` = 2U.
  - `DEVICE_SN_PAYLOAD_DIGITS` = 11U.
  - `DEVICE_SN_STR_LEN` and `DEVICE_SN_TEXT_LEN` are removed, and every user is updated.
- **Unchanged:**
  - `device_sn_check_digit()`
  - `device_sn_yyww_from_date()`
  - `device_sn_yyww_from_build_date()`
  - `device_sn_parse_sequence()`
  - `device_sn_parse_yyww()`
- **New: `device_sn_parse_family(const char *text, uint8_t *family)`.** It accepts exactly two
  decimal digits.
- **`device_sn_format(family, yyww, sequence, out, cap)`:**
  - It writes the 12 digits followed by a NUL, so `cap` must be at least `DEVICE_SN_STR_SIZE`.
    Only the first `DEVICE_SN_LEN` bytes are stored.
  - **Self-check, as the spec requires:**
    1. Build the string in a local buffer.
    2. Recompute C from the 11 formatted digits.
    3. Confirm the result with `device_sn_validate()`.
    4. Copy the string to `out` only if both checks pass. Otherwise return `SL_STATUS_FAIL` and
       leave `out` untouched.
- **`device_sn_validate(const char *sn)`:**
  - Reads `DEVICE_SN_LEN` bytes, and all of them must be digits.
  - Checks that C is right.
  - Does not range-check YYWW, as before.
- **`device_sn_to_printable()`:** copies up to `DEVICE_SN_LEN` bytes, replaces every
  non-printable byte with `.`, and terminates the copy. `cap` must be at least
  `DEVICE_SN_STR_SIZE`.

### services: `nvm_store`

- **Read:**
  - Accept only an `NVM3_OBJECTTYPE_DATA` object of exactly `DEVICE_SN_LEN` bytes.
  - Any other length gives `SL_STATUS_INVALID_COUNT`, and the stored length is reported.
  - Callers pass a buffer of `DEVICE_SN_STR_SIZE` bytes. The read adds a NUL after the 12 bytes
    in RAM only.
- **Write:** `nvm3_writeData()` with `DEVICE_SN_LEN` bytes, then a read-back and a `memcmp`.

### app, hex build: `sn_burner`, `app.c`

- **`SN_BURNER_FAMILY`:**
  - It is stringified like `SN_BURNER_SEQUENCE`, so `07` is never read as an octal number.
  - A `_Static_assert` checks that it is two characters long.
  - `burn()` parses it at run time with `device_sn_parse_family()`.
- **`app.c`:** in the PROVISION branch, `#ifndef SN_BURNER_FAMILY` gives an `#error`, whose
  message names `tools/provision.py --family`.
- **Log lines:**
  - Success stays `serial number written: <12 digits>`.
  - A new reason for `serial number not written`: an invalid family code.

### app, CLI build: `cli.c`, `cli_show.c`

- **Command tree:**
  - `device` -> `serial-number` -> `<FF>` -> `<SSSSS>`, and `<SSSSS>` carries
    `CMD_DEVICE_SN_SET`.
  - The optional `week` -> `<YYWW>` continuation is unchanged and also carries
    `CMD_DEVICE_SN_SET`.
  - The hand-kept `child_count` values are updated.
- **`cmd_device_sn_set(family_text, sequence_text, yyww_text)`:** parses the family code first.
  The dispatch passes `args[0..2]`.
- **Output:** the preview, the overwrite warning, the `[OK]` line and `show device serial-number`
  all print the 12-digit form.

### Build: `cmake_gcc/CMakeLists.txt`, allowed macro sections only

- In both macro blocks, add
  `$<$<NOT:$<STREQUAL:${SN_BURNER_FAMILY},>>:SN_BURNER_FAMILY=${SN_BURNER_FAMILY}>`, following
  the `SN_BURNER_SEQUENCE` pattern.

### Host script: `tools/provision.py`

- **Arguments:** a new required `--family FF`, matched against `^\d{2}$`, and passed to CMake as
  `-DSN_BURNER_FAMILY=FF`.
- **Expected S/N:** `FF + YYWW + SSSSS + check_digit(FF + YYWW + SSSSS)`. Before printing, the
  script checks it again with the same function.
- **Log match:** `WRITTEN_RE` becomes `serial number written: (\d{12})`.
- **Post-build check:** the `compile_commands.json` check also confirms `SN_BURNER_FAMILY`.
- **Image name:** `xbee_provision_<FFYYWWSSSSSC>.hex`.

### Tests: `test/test_device_sn.c`

- **Check digit:**
  - `0794` -> 5 stays.
  - New 11-digit vectors, cross-checked against `provision.py`'s `check_digit()`.
  - `18072600123` -> 0.
- **Family parsing:** wrong length and non-digit input are rejected.
- **Format:** the output is exactly 12 digits followed by a NUL.
- **Validate:** each of these is rejected:
  - a dash
  - a letter
  - a wrong C
  - the old `YYWW-NNNNN-C` form

### Documentation

- `readme.md`: the serial-number section and the `provision.py` usage, with `--family`.
- `CLAUDE.md`, Project facts: key `0x00003` holds 12 bytes, `FFYYWWSSSSSC`, with no NUL.
- A history entry under `docs/history/`.

## Simplicity Studio changes required (PRIME RULE)

None. The NVM3 configuration and the key are unchanged.

## Resource impact

- Flash grows by a few bytes, for family parsing and the self-check.
- RAM buffers grow by 1 byte each.
- There are no new peripherals, and energy modes are unchanged.

## Risks and open questions

1. **The specification's example is wrong.** `180726001234` gives C = 4, but the check digit of
   `18072600123` is 0. Both this firmware's algorithm and `provision.py` give 0, so the correct
   example is `180726001230`. Recommendation: correct the spec text. This work does not edit it.
2. **"ISBN-13-style" in the spec is inaccurate.** ISBN-13 uses a weighted mod 10 check, not ISO
   7064 MOD 11-10. Only the wording is affected.
3. **WTX-FW must change.** Its `DEVICE_SN_STR_LEN` (13) and read code must accept 12 bytes with no
   NUL, and add their own terminator before display. Until they do, WTX-FW reports the new
   object as "not set". That work is outside this repository.
4. **Old-format units.** A unit burned with the 13-byte `YYWW-NNNNN-C` value reads back as "wrong
   length, not set". Burning it again overwrites the value.
5. **YY** stays the ISO week-numbering year, so YYWW is always a real ISO week. The spec says
   "year of manufacture" and does not settle the year-boundary case.
6. **The `FF` value is still TBD.** Once it is assigned, record it here and in `readme.md`.

## Verification

1. **Host tests:**
   `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure`.
   This needs a host C compiler. If there is none, the history entry says so.
2. **Python cross-check:** compare `provision.py` `check_digit()` with the C test vectors.
3. **Default CLI build:** `cd cmake_gcc && cmake --workflow --preset project`, with no warnings.
4. **PROVISION build without `SN_BURNER_FAMILY`:** it must stop at the `#error`.
5. **`python tools/provision.py --family 18 --sequence 00123`:** builds and prints the expected
   12-digit S/N. Answer N at the flash prompt.
6. **On target**, only with the owner's go-ahead:
   - **CLI:** `device serial-number 18 00123`, then `show device serial-number`.
   - **Hex:** flash-and-watch, then Commander's NVM3 parse shows key `0x3` holding 12 bytes.
