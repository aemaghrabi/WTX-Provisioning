# XBee provisioning application, phase 1: command table helpers

Date: 2026-09-19
Related plan: docs/plan/2026-09-19-xbee-provision-app.md

## Summary

Prepared `xbee_at_table` for the provisioning application: the factory default
field widened to 64 bits, the `IA` default corrected to the full 64-bit value
the manual gives, and three helpers added that answer the questions the
provisioner asks of every command. The Command mode line buffer grew so that a
read of the File System Public Key fits.

No behaviour of the existing bring-up application changes.

## Motivation

Provisioning writes only the parameters that deviate from the factory default,
so it needs to ask the table three things: whether a command belongs in a stored
configuration at all, whether a given value is that command's default, and
whether two values of the same command are the same value. None of those existed.

The `IA` default exposed a defect while the first of those was being written.
The manual gives it as `0xFFFFFFFFFFFFFFFF`, I/O line passing disabled (line
6803), but `default_value` was `uint32_t`, so the table held `0xFFFFFFFF`. A
header leaving `IA` at its documented default would have been counted as a
deviation, written needlessly, and then failed the read-back comparison.

## Changes

| File | Change |
| --- | --- |
| `src/services/inc/xbee_at_table.h` | `default_value` widened from `uint32_t` to `uint64_t`, with the reason recorded on the field. Declarations for `xbee_at_table_is_provisionable()`, `xbee_at_table_is_default()` and `xbee_at_table_values_equal()` |
| `src/services/src/xbee_at_table.c` | `IA` default corrected to `0xFFFFFFFFFFFFFFFF`. The three helpers implemented, with a shared `type_is_numeric()` |
| `src/services/inc/xbee_cmd_mode_config.h` | `XBEE_CMD_MODE_LINE_MAX` 128 to 160 |
| `test/test_xbee_at_table.c` | `IA` added to the known-entry checks; new `test_provisionable`, `test_is_default` and `test_values_equal` |

### What the helpers decide

`xbee_at_table_is_provisionable()` accepts a command that holds a writable value
the module keeps and whose factory default the manual states. It rejects
executable commands and text subcommands by type, and read-only, volatile and
no-default commands by flag. That is 108 of the 147 commands, a count the test
pins so a later table change cannot silently alter the provisionable set.

`xbee_at_table_is_default()` and `xbee_at_table_values_equal()` compare integers
numerically rather than byte by byte. This is not cosmetic: Command mode prints
a parameter as hexadecimal with no leading zeros (manual lines 3090 to 3093), so
a two-byte parameter such as `CT` comes back in one byte. A byte comparison
would report every such parameter as a mismatch and provisioning would rewrite
the whole configuration on every boot.

### Why the line buffer grew

`FK`, the File System Public Key, is 65 bytes and prints as 130 hexadecimal
characters in one line (manual lines 5896 to 5916). The 128-character buffer
would have truncated it. `FK` is provisionable, so the verification pass reads
it back. The command buffer at 160 already covered the write.

## Simplicity Studio changes (made by the user)

None. Application code only.

## Verification

**Build and host tests. Nothing was run on hardware.**

- Host tests: six suites, all passing, including the three new cases.
- Target build clean under the project's warning settings. Text grew from
  45 832 to 46 420 bytes, 588 bytes, which is the widened default field across
  147 entries plus the three helpers.
- `git status` shows no Studio-owned file modified.

## Known limitations and follow-ups

- `xbee_at_table_is_default()` assumes every string parameter defaults to one
  character, which holds for all five in the table (`NI`, `BI`, `LX`, `LY`,
  `LZ`, all an ASCII space). A future string command with a longer default
  would need the table to carry the default text rather than one character.
- The `min` and `max` bounds stay 32 bits. They are unused for 64-bit
  parameters, which `xbee_at_table_validate_set()` checks by width alone, so
  `IA` and `US` are not range checked. Widening them was not needed for
  provisioning and was left out.
