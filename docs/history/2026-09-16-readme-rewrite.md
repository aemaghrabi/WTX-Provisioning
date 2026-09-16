# Rewrite readme.md for project scope

Date: 2026-09-16
Related plan: none

## Summary
Replaced the "Empty C Example" template text in `readme.md` with a description of the
provisioning firmware: scope, hardware interface, software, repository layout, development rules,
build steps, and production flow.

## Motivation
The readme still described the Simplicity Studio empty example. The user defined the project
scope: provisioning firmware flashed on brand-new boards to set them up for the main firmware.

## Changes
| File | Change |
| --- | --- |
| `readme.md` | Rewritten. Scope as confirmed by the user: hardware self-test, read/record IDs, XBee radio configuration, write MCU NVM data; result reported via UART log to a PC and via debugger/RTT; main firmware flashed afterwards over SWD. Audience: developers and production. Undefined details marked TBD. |

## Simplicity Studio changes (made by the user)
None.

## Verification
Static review only. No firmware build; no source code changed.

## Known limitations and follow-ups
Open items marked TBD in `readme.md`, to be defined with the user:
- XBee parameter set to configure.
- Identifiers to read and where they are recorded.
- MCU NVM data content and storage layout.
- Self-test pass/fail criteria.
- Reporting interfaces: UART instance and pins for the log, RTT setup (both need Simplicity Studio
  configuration).
- Detailed production procedure.
