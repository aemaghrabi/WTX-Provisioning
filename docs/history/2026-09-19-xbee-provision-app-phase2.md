# XBee provisioning application, phase 2: forced Command mode session

Date: 2026-09-19
Related plan: docs/plan/2026-09-19-xbee-provision-app.md

## Summary

The facade can now be told to route every request through a Command mode
session, whatever serial mode the module was detected in. Four calls were added:
`xbee_cmd_session_open()`, `xbee_cmd_session_status()`, `xbee_cmd_session_close()`
and `xbee_cmd_session_is_open()`.

Until now Command mode was used only when the module was found in Transparent
mode, because that was the only way to reach its parameters. Provisioning needs
it in every mode.

## Motivation

Provisioning writes a batch of parameters and commits them with one write to
flash. Two properties of Command mode make that possible, and neither holds for
an API frame:

- A parameter set in Command mode is staged and does not take effect until the
  session ends (manual lines 3101 to 3110). A Local AT Command Request applies
  immediately (manual lines 7480 to 7483). Without staging, writing `AP` or `BD`
  would change the interface underneath the run and the remaining writes would
  be lost.
- `RE` and `R1` restore defaults without leaving the session (manual line 7092),
  so the restore, the writes and the flash commit are one uninterrupted
  sequence.

Command mode is available from every operating mode over the UART (manual line
3041), so forcing it is legitimate whether the module is at `AP` 0, 1 or 2.

## Changes

| File | Change |
| --- | --- |
| `src/services/inc/xbee.h` | The four declarations, with what each returns and why the session exists |
| `src/services/src/xbee.c` | `cmd_session_forced` flag; new `use_cmd_transport()` consulted by `dispatch_request()`, `poll_request()`, `submit()` and `xbee_process()`; the four functions; the flag cleared in `xbee_init()`, `xbee_deinit()` and `xbee_hw_reset()` |

### How routing changes

`use_cmd_transport()` replaces the three separate `mode_is_api(detected_mode)`
tests that decided which transport carries a request. It returns true when the
module is in Transparent mode, as before, or when a session has been forced.

Three consequences were handled deliberately:

- **Only one transport runs while forced.** `xbee_process()` stops driving the
  API transport, because both read from the same receive ring and the API parser
  would consume the session's reply lines.
- **The Command-mode-only refusal is lifted while forced.** `submit()` rejects
  commands flagged Command mode only when they would travel as an API frame. A
  forced session satisfies the requirement, so the refusal no longer applies.
- **A session the module closes by itself releases the force.** The module drops
  out of Command mode after `CT` with no input (manual lines 3062 to 3065).
  `xbee_process()` notices the transport going idle with no request in flight and
  clears the flag, so a stalled caller cannot leave the facade sending into a
  closed session.

### Why close does not send an ordinary CN request

`xbee_cmd_session_close()` calls the transport's own `xbee_cmd_mode_exit()`
rather than `xbee_at_exec()` with the exit command. Sending it through the
generic path would write the same bytes but leave the transport in its session
state, so it would believe a closed session was still open and the next command
would be sent into nothing. The transport's exit moves its state machine and
abandons anything still in flight.

## Simplicity Studio changes (made by the user)

None. Application code only.

## Verification

**Build and host tests. Nothing was run on hardware.**

- Target build clean. Text grew from 46 420 to 46 484 bytes, 64 bytes.
- Host tests: six suites, all passing. The facade is not host-built, so these
  cover the unchanged transports rather than the new routing.
- `git status` shows no Studio-owned file modified.

## Known limitations and follow-ups

- **The new routing has no automated test.** `xbee.c` depends on the drivers and
  is built only for the target, so the four calls were checked by static review.
  The on-target check in the plan's phase 2 list covers it: open a session from
  API mode 1, read a parameter, close, and confirm API frames work afterwards.
- While a session is forced the API transport is not driven at all, so an
  unsolicited frame arriving in that window is lost. Nothing sends to the module
  during provisioning, and the receive ring is flushed by the reset that follows,
  so this does not affect the intended use. It would matter if a caller forced a
  session on a module that was also carrying traffic.
