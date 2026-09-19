# XBee provisioning application, phase 4: the application and the app selection

Date: 2026-09-19
Related plan: docs/plan/2026-09-19-xbee-provision-app.md

## Summary

Added `xbee_provision`, the application that brings the module to the
configuration in the header, and a build-time switch in `app.c` that chooses
between it and the existing bring-up application. Provisioning is the default.

The XBee stack is now used for what the project exists to do.

## Motivation

Phases 1 to 3 built the parts. This is the sequence that uses them.

## Changes

| File | Change |
| --- | --- |
| `src/app/inc/xbee_provision.h` | New. The four calls and the result enumeration |
| `src/app/src/xbee_provision.c` | New. The sequence, the logging and the build-time checks against the Simplicity Studio link settings |
| `app.c` | Selects one application from `XBEE_APP` |
| `cmake_gcc/CMakeLists.txt` | Registers `xbee_provision.c`, and defines `XBEE_APP` for both the executable and the generated `slc` library |
| `src/services/inc/xbee.h`, `src/services/src/xbee.c` | New `xbee_at_exec_timeout()`; `submit()` takes the timeout rather than always using the local one |

### The sequence

Bring up the module and report what it is, open a Command mode session, read
every readable configured parameter and compare. If all match, close and stop:
nothing is written and no flash cycle is spent. Otherwise restore the module's
defaults, write only the parameters the header sets away from the default,
commit them with one write to flash, close the session, reset the module, and
read everything back to prove the configuration took.

This follows the idiom the manual gives for exactly this job: restore defaults,
write, then reset, with the changes applying on the reset (lines 3113 to 3115).

Every outcome reached with a session open goes through one close path, so the
module is never left sitting in Command mode waiting out its own timeout.

### Why a longer timeout was needed

The facade applied one fixed timeout, a second, to every request. Restoring
defaults and writing to flash both erase and rewrite flash and can take much
longer. The manual is explicit that nothing may be sent to the module between
the write command and its reply (lines 7093 to 7095), so giving up early and
sending the next thing is precisely the hazard it warns about.

`xbee_at_exec_timeout()` was added for those two commands, with five seconds.
`submit()` now carries the timeout instead of hard-coding it; every existing
caller passes the same value as before, so nothing else changes.

### Build-time checks

`xbee_provision.c` refuses to compile when the configuration disagrees with the
generated Simplicity Studio configuration of the XBEE instance. The baud rate is
mapped to the module's code and compared, so setting one without the other fails
the build rather than producing a module that comes back from its reset talking
at a rate this MCU is not listening at.

Parity and stop bits are asserted to be no-parity and one stop bit. The Studio
values for those are enumerator tokens rather than numbers, so the preprocessor
cannot compare them; that limitation is recorded below.

The API mode is restricted to 0, 1 or 2, and the restore command to one of the
two defined values.

### Application selection

`app.c` picks the application from `XBEE_APP`, set in the permitted macro section
of the build file. The definition is given to both the executable and the
generated `slc` library, because `app.c` is compiled in the latter and does not
inherit the executable's definitions.

Bring-up remains as the diagnostic build: it writes nothing to the module, which
is what you want when a board will not talk at all.

## Simplicity Studio changes (made by the user)

None. Application code only.

## Verification

**Build and host tests. Nothing was run on hardware.**

- Target build clean with provisioning selected. Text grew from 46 484 to
  51 620 bytes, about 5 kB for the sequence, the configuration table and the log
  strings. The symbol table confirms the provisioning application is linked and
  the unused bring-up application is not.
- Target build clean with bring-up selected, and the bring-up application linked
  instead.
- The baud rate check was confirmed to fire: setting the configured rate to
  115200 against the instance's 9600 failed the build with the intended message.
- Host tests: seven suites, all passing.
- `git status` shows no Studio-owned file modified.

### Defect found and fixed during review

The helper that formats a parameter value for the log returned a single shared
buffer, and the mismatch messages call it twice in one statement, once for what
the module holds and once for what was configured. Both fields would have
printed the same value, which is actively misleading in exactly the message
someone would rely on to diagnose a failure. The helper now takes the caller's
buffer.

## Known limitations and follow-ups

- **None of this has run on hardware.** The on-target checks in the plan are
  outstanding, and the guard-time fix the stack depends on
  (`docs/history/2026-09-19-cmd-mode-guard-time-fix.md`) is itself still
  unconfirmed. Command mode entry has to work before provisioning can.
- **Parity and stop bits are not compared against Studio**, only asserted to be
  the values the instance currently uses. Changing the instance to use parity
  would not fail the build. Comparing them properly would mean including the
  EUSART headers in an application file.
- **Whether the factory restore keeps the Command mode session open is not
  documented.** The manual states it for the ordinary restore (line 7092) but
  not for the factory one. If it closed the session, the writes that follow
  would go unanswered and the run would stop with a write failure, so the
  failure is visible rather than silent.
- **The AES key cannot be verified**, because the module never reports it. The
  readable parameters are trusted to decide whether a run is needed, as agreed.
- Parameters the module rejects on read, such as the SPI pin commands on a
  through-hole variant, are counted and warned about but not treated as
  failures. A configured value for such a parameter would still fail the run at
  the write, which is the correct outcome.
