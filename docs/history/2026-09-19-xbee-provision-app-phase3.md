# XBee provisioning application, phase 3: configuration header and compiled table

Date: 2026-09-19
Related plan: docs/plan/2026-09-19-xbee-provision-app.md

## Summary

Added the header that describes the XBee configuration this board is provisioned
to, one macro per parameter for all 108 the module stores, and the compiled
table that turns those macros into the exact bytes that travel to the module.

Every macro ships holding the manual's factory default, so an unedited header
describes a factory module and provisioning would write nothing.

## Motivation

The configuration has to be stated in one place a person can read and edit,
covering every parameter rather than a chosen few, and it has to be checkable
before anyone burns a flash cycle on a board.

## Changes

| File | Change |
| --- | --- |
| `src/app/inc/xbee_provision_config.h` | New. 108 parameter macros grouped by the manual's categories, each with its meaning, range, default and the manual lines, plus the restore and power-cycle options |
| `src/app/src/xbee_provision_list.h` | New, private. The parameter list as one X-macro naming each parameter's short name, command identifier and width |
| `src/app/inc/xbee_provision_table.h` | New. The `xbee_prov_param_t` entry and the accessor |
| `src/app/src/xbee_provision_table.c` | New. Expands the list twice, into the value arrays and then the table |
| `test/test_xbee_provision_table.c` | New. Coverage, value validity, the deviation report and the link settings |
| `test/test_util.h` | New `TEST_FAIL`, so a failure inside a loop can name the command rather than the condition |
| `test/CMakeLists.txt` | Registers the new test |
| `cmake_gcc/CMakeLists.txt` | Registers `xbee_provision_table.c` |

### How the header is compiled

Integers are given as plain numbers whatever their width and laid out
big-endian by the `XBEE_PROV_BE1/2/4/8` initialisers, so the table holds no
value that needs converting at run time. Strings are given as literals and
stored with the terminator, which is excluded from the length. Byte parameters
are initialiser lists written at their full width, with omitted bytes zero.

The single X-macro list means a parameter is named once. It cannot appear in the
value arrays and be missing from the table, or the reverse.

### What the tests check

- **Coverage is exact in both directions.** Every configured command can be
  provisioned, none appears twice, and every command the command table says can
  be provisioned has a configured value. The last of those is the one that
  matters over time: a command added to the command table without a value here
  would never be written and never be checked, and the configuration would
  quietly stop being complete.
- **Every value is one the module would accept**, by the same range and width
  checks the transports apply before sending.
- **A string too long for its command fails to compile**, through a static
  assertion, rather than being silently truncated.
- **The write set is reported rather than asserted.** The test prints each
  parameter that deviates from the factory default and its value. That list is
  exactly what a provisioning run would write, so a change to the configuration
  shows up as a reviewable diff of what the module will be told, without anyone
  running a board.

The deviation list was deliberately not made an assertion. Asserting that the
header is all default would have failed the moment anyone configured the product,
which is the header's entire purpose.

## Simplicity Studio changes (made by the user)

None. Application code only.

## Verification

**Build and host tests. Nothing was run on hardware.**

- Host tests: seven suites, all passing. The new suite makes 6 336 checks.
- The tests were confirmed to fail on bad input rather than passing vacuously:
  an out-of-range channel is rejected by the command table with
  `SL_STATUS_INVALID_RANGE`, and an over-long node identifier fails the static
  assertion at compile time.
- The report was confirmed against an edited header: setting the node
  identifier, the channel and the API mode printed exactly those three writes.
- Target build clean. The image is unchanged at 46 484 bytes because nothing
  references the table yet, so the linker discards it.
- `git status` shows no Studio-owned file modified.

## Known limitations and follow-ups

- The link settings, `BD`, `NB` and `SB`, are checked against fixed values in
  the host test. The check against the generated Simplicity Studio configuration
  happens in `xbee_provision.c`, which arrives in phase 4.
- Secure Session and Bluetooth verifier material is covered as opaque byte
  parameters. The manual describes them as slices of a longer verifier, so
  setting them correctly means knowing how that verifier was derived. The header
  documents the width and leaves the content to whoever configures it.
