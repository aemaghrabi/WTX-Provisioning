# Compare byte parameters without leading zeros

Status: Done, pending host-test and on-target verification
Date: 2026-10-06
Related history: docs/history/2026-10-06-bytes-compare-leading-zeros.md

Implemented as designed. The firmware builds with no warnings. The updated host test passes a
syntax check with the ARM compiler, but the tests have not been run, because this machine has no
host compiler.

## Goal

Provisioning must not fail verification on a byte parameter whose value is correct.

Observed on target: `E provision: *V reads back as 00, expected 000000...`. The module's `*V`
is all zeros, as configured, but verification still reports a mismatch.

Measurable outcome:
- With the default configuration, the verification pass accepts `*V`, `*W`, `*X` and `*Y`.
- A module that is already provisioned ends with "already provisioned, nothing written".

## Background and references

- In Command mode the module prints a parameter in hexadecimal without leading zeros. The
  numeric comparison already relies on this (`src/services/inc/xbee_at_table.h`,
  `xbee_at_table_values_equal()` description). Provisioning reads in Command mode, so an
  all-zero 32-byte verifier comes back as `0`, which decodes to one byte, `00`.
- `docs/manuals/xbee_90002273_ref_manual.md`, lines 5529 to 5537: `*V`, `*W`, `*X` and `*Y` are
  each 32 bytes of the 128-byte verifier, and the default is all zeros. The manual does not say
  directly that byte parameters lose their leading zeros. That behaviour comes from the
  on-target log above.
- `xbee_at_table_values_equal()` (`src/services/src/xbee_at_table.c`) compares
  `XBEE_AT_TYPE_BYTES` exactly, length included. That is why one byte `00` never equals
  32 bytes of `00`.

## Design

In `xbee_at_table_values_equal()`, a `XBEE_AT_TYPE_BYTES` value is compared after stripping
leading zero bytes from both sides:
- What remains must have the same length and the same content (`memcmp`).
- Two values that are all zeros, of any length, are equal.

Because the comparison works on the stripped values, it stays exact for real verifier and key
values. A value such as `00 1F ...` compares equal to the same value printed without its leading
`00`, and to nothing else.

What does not change:
- Strings still compare exactly, because a string's length is part of its value.
- Numeric types keep their existing numeric comparison.

The audit and the verification pass in `src/app/src/xbee_provision.c` both use this function, so
both are fixed. The engine itself needs no change.

`test/test_xbee_at_table.c`, `test_values_equal()`:
- The existing assertion `!values_equal(KY, {2B}, {00 2B})` encodes the old byte-exact behaviour.
  It is changed to expect equality.
- New cases:
  - 32 zero bytes against one byte `00`;
  - 32 zero bytes against an empty value;
  - a non-zero value whose leading zero bytes have been stripped;
  - values that differ only after their leading zeros.

The header description is updated to match.

## Simplicity Studio changes required (PRIME RULE)

| Tool | Component / instance | Setting | Current value | Required value |
| --- | --- | --- | --- | --- |

None. The change is application code only.

## Resource impact

A few instructions of flash. No RAM, peripheral or energy-mode impact.

## Risks and open questions

- The CLI uses `xbee_at_table_values_equal()` only through the provisioning engine, so nothing else
  changes behaviour.
- In API mode the module may return byte parameters at full length. The comparison accepts that
  too, because stripping makes it independent of padding.

## Verification

1. Firmware build of the CLI variant with zero warnings.
2. Host tests `test_xbee_at_table` pass. This machine has no host compiler, so they are not run
   here.
3. On target, using `xbee provision all` or the provisioning build:
   - verification passes with the default all-zero verifiers;
   - a second run reports "already provisioned, nothing written".
