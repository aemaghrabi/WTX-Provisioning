# Compare byte parameters without leading zeros

Date: 2026-10-06
Related plan: docs/plan/2026-10-06-bytes-compare-leading-zeros.md

## Summary

`xbee_at_table_values_equal()` now strips leading zero bytes from both `XBEE_AT_TYPE_BYTES`
values before comparing them. An unset verifier read back in Command mode as one `00` byte now
equals the configured 32 zero bytes.

## Motivation

The project owner reported this on target: `E provision: *V reads back as 00, expected
000000...`. Command mode prints values without leading zeros, so the byte-exact comparison failed
on a correct value:
- verification failed;
- the audit always counted `*V`, `*W`, `*X` and `*Y` as different, so a module that was already
  provisioned was restored and written again on every run.

## Changes

| File | Change |
| --- | --- |
| `src/services/src/xbee_at_table.c` | `xbee_at_table_values_equal()`: leading zero bytes are stripped from both `BYTES` values before the length and `memcmp` check. Strings and numeric types are unchanged |
| `src/services/inc/xbee_at_table.h` | Function description updated |
| `test/test_xbee_at_table.c` | `test_values_equal()`: the old byte-exact assertion now expects equality. New `*V` cases cover 32 zeros against one `00` byte and against an empty value, a value with and without leading zeros, a difference after the leading zeros, and a trailing zero that still counts |
| `docs/plan/2026-10-06-bytes-compare-leading-zeros.md` | New plan |
| `docs/history/2026-10-06-bytes-compare-leading-zeros.md` | This entry |

## Simplicity Studio changes (made by the user)

| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |

None.

## Verification

- **Firmware build:** the CLI variant builds with no warnings under `-Wall -Wextra`.
- **Host test:** `test/test_xbee_at_table.c` passes a syntax check (`arm-none-eabi-gcc -fsyntax-only
  -Wall -Wextra`). It has not been compiled or run as a host test, because there is no host
  compiler on this machine.
- **On target:** not yet run.

## Known limitations and follow-ups

- Run the host tests (`ctest --test-dir test/build`) on a machine with a host compiler.
- On target: run `xbee provision all` twice. The first run should pass verification and the
  second should report "already provisioned, nothing written".
