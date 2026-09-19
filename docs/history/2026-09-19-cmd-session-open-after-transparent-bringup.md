# Fix: a Command mode session could not be opened after a Transparent mode bring-up

Date: 2026-09-19
Related plan: docs/plan/2026-09-19-xbee-provision-app.md

## Summary

Provisioning did not run at all when the module was in Transparent mode. It
stopped immediately after bring-up with "could not start a Command mode
session". In an API mode the same build worked.

The cause was a race of the facade's own making. Bring-up closes the Command
mode session it used to read the module's parameters and reports itself ready in
the same iteration, so the exit is still in flight when the application first
sees a ready facade. Opening a session demanded an idle transport and failed
outright rather than waiting for one.

Fixed by letting a requested session wait for the transport to fall idle before
sending the escape sequence. The facade also gained its first host test suite,
driven against a simulated module, which reproduces this defect.

## Why it only happened in Transparent mode

`process_bringup()` ends a Transparent mode bring-up like this:

```c
if (detected_mode == XBEE_MODE_TRANSPARENT) {
  (void)xbee_cmd_mode_exit(NULL);
}
...
state = XBEE_STATE_READY;
```

The exit leaves the transport in its exiting state, waiting for the module's
reply, and the facade is marked ready on the next line. The application polls
readiness every super-loop iteration, so it calls `xbee_cmd_session_open()`
before that reply arrives. Open called `xbee_cmd_mode_enter()`, which requires
the idle state and returns `SL_STATUS_INVALID_STATE` otherwise, and the
provisioning sequence stopped with a session failure.

In an API mode bring-up never opens a session, so the transport is already idle
and the same code worked.

## Changes

| File | Change |
| --- | --- |
| `src/services/src/xbee.c` | The single forced flag replaced by a four-state `cmd_session`; new `advance_cmd_session()` driven from `xbee_process()`; open, status and close reworked around it |
| `src/services/inc/xbee_config.h` | New `XBEE_CMD_SESSION_OPEN_TIMEOUT_MS`, default 10 000 |
| `test/fake_drivers.c`, `test/fake_drivers.h` | New. Host stand-ins for the supply, reset and sleep lines, and for the log |
| `test/test_xbee_facade.c` | New. A simulated module and five tests |
| `test/CMakeLists.txt` | Registers the new suite |

### How a session is opened now

`xbee_cmd_session_open()` records the request and a deadline, then returns. It no
longer sends anything. `xbee_process()` starts the escape sequence once the
transport is idle, and retries it if the module does not answer, until the
deadline. The states are explicit: none, pending, entering, open.

That also fixed a smaller defect in the same area. Previously, if entry timed
out, whether the caller saw `SL_STATUS_TIMEOUT` or `SL_STATUS_INVALID_STATE`
depended on whether `xbee_process()` or `xbee_cmd_session_status()` noticed
first. The deadline now decides, so the answer is the same either way.

An empty `if` body was also removed from the close path, left behind when an
unused flag was deleted during phase 2.

## The facade now has host tests

Two defects in this facade have now reached hardware: the guard time measured
from the wrong moment, and this one. Neither was visible to the transport tests,
because both live in the facade's sequencing rather than in either transport.

`test_xbee_facade.c` drives the real facade against a simulator that plays the
module: it requires a full guard time of silence before it answers the escape
sequence, replies to AT commands, and answers Local AT Command Request frames
when it is in an API mode. `fake_drivers.c` stands in for the three control line
drivers, exposing each line so a test can check it.

The five tests cover a Transparent mode bring-up, opening a session at the exact
moment readiness appears, opening one from an API mode and confirming the command
travels as AT text, closing one, and a module that never answers.

Escaping is not modelled, so the API tests use mode 1. Mode 2 is covered by the
codec's own tests.

## Simplicity Studio changes (made by the user)

None. Application code only.

## Verification

**Build and host tests. The fix has not been confirmed against hardware.**

- The regression test was confirmed to fail against the previous code: opening
  returns `SL_STATUS_INVALID_STATE` where the test expects success, and only the
  Transparent mode test fails, which matches the report exactly.
- Host tests: eight suites, all passing.
- Target build clean. Text grew from 51 620 to 51 692 bytes.
- `git status` shows no Studio-owned file modified.

## Known limitations and follow-ups

- **Still to be confirmed on hardware.** A Transparent mode module should now
  log the session opening and go on to read its configuration.
- The simulator answers instantly. It does not model the module's processing
  delay, so a test cannot show that a timeout is generous enough, only that one
  exists and fires.
- The retry of the escape sequence within the open deadline is bounded by time
  rather than by a count. With the default guard time that allows roughly four
  attempts.
