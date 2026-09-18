# XBee 3 driver stack, phase 4: Command mode transport

Date: 2026-09-18
Related plan: docs/plan/2026-09-18-xbee3-driver-stack.md

## Summary

Added `xbee_cmd_mode`, the transport that reaches the module's parameters when
it is in Transparent mode and no API frames are available. It drives the escape
sequence with its guard times, writes AT commands as text, assembles and
classifies reply lines, tracks the module's own session timeout, and carries
Transparent mode data when no session is open.

## Motivation

A module in Transparent mode, which is the factory default, cannot be configured
any other way. Provisioning has to work on a module straight out of its tray, so
the stack needs this path as much as it needs the API one.

## Changes

| File | Change |
| --- | --- |
| `src/services/inc/xbee_cmd_mode.h`, `xbee_cmd_mode_config.h`, `src/services/src/xbee_cmd_mode.c` | New: entry sequence, command writing, line assembly and classification, session timeout, Transparent data path |
| `test/test_xbee_cmd_mode.c`, `test/CMakeLists.txt` | New test suite |
| `cmake_gcc/CMakeLists.txt` | Registered the new source |

Design points worth recording:

- **The guard time is measured from our own transmissions.** The manual asks for
  silence before and after the escape sequence, meaning no input to the module
  (lines 3044 to 3051). The transport records when it last sent a byte and waits
  a full guard time from that point, so Transparent data sent moments earlier
  correctly delays the sequence. Received bytes do not restart the guard, because
  they are not input to the module.
- **Entry retries rather than failing when the transmitter is busy.** Extra
  silence before the sequence does no harm, so a busy transmitter simply defers
  the attempt to the next iteration.
- **The session deadline is tracked locally with a margin.** The transport gives
  up on the session half a second before the module's own timeout would, so a
  command is never sent into a window that has just closed. Every accepted
  command pushes the deadline out, exactly as the module's own timer does.
- **An over-long line is dropped, not truncated.** A reply longer than the line
  buffer is discarded and the command times out, rather than returning a value
  that is silently missing its tail.
- **The line classifier is a pure function.** Separating it made the File System
  error rule testable in isolation, which matters because the manual's detection
  rule, an uppercase E at the start, also matches the word ERROR.

## A subtlety in the File System error rule

The manual says a File System command reports failure as a named code and that
you can tell one from an ordinary reply by checking whether the first letter is
an uppercase E (lines 5820 to 5826). Taken literally that also matches `ERROR`
itself, and it matches ordinary hexadecimal values that begin with E, which the
module produces routinely. The classifier therefore checks for the exact word
`ERROR` first, then requires the fuller documented shape for a File System error:
an uppercase E, one or more uppercase letters or digits, then a space. The test
suite pins down the cases that must not be misread, including `EF12` as a plain
value and `ERRO` as neither.

## Simplicity Studio changes (made by the user)

None. This phase is application code only.

## Verification

**Build and host tests. Nothing in this phase has been run on hardware.**

- Target build clean under `-Wall -Wextra -Os`.
- `xbee_cmd_mode` object: 1866 bytes of text, 317 bytes of static RAM.
- Host tests: `test_xbee_cmd_mode`, 403 checks, all passing. It covers the line
  classifier against every reply shape the module produces, the full entry
  sequence, the guard time measured from a previous transmission, entry giving up
  when the module does not answer, reading and writing parameters in both
  hexadecimal and text form, a rejected write, a File System error, a command
  timeout, a multi-line reply ended by the empty line that closes a discovery,
  a multi-line reply that nothing answers, the exit command, the session lapsing
  on the module's timeout, the session being kept alive by a steady stream of
  commands, a module configured with the manual's own non-default guard time and
  command character, the Transparent data path in both directions, abandoning a
  session without telling the module, replies split across several iterations,
  a line feed alongside the carriage return, an over-long line, and entry
  retrying against a busy transmitter.
- All six suites pass together.
- Static review: no blocking waits; the transport never transmits while a guard
  timer runs; every copy bounded by an explicit length check.
- `git status` shows no Studio-owned file modified.

## Known limitations and follow-ups

- **Not tested on hardware.** In particular the guard timing has only been
  exercised against a virtual clock. The plan's on-target check, entering Command
  mode on a module at AP=0 and reading the version and serial number, is still
  outstanding.
- One command at a time. Command mode is inherently serial, so this is a
  property of the protocol rather than a limitation of the implementation.
- The transport does not parse the fields of a Node Discover reply. It hands the
  lines over as they arrive and leaves the field layout, documented at manual
  lines 5008 to 5021, to the caller.
- The alternative Command mode entry the manual describes, a serial break at
  power-up, is not implemented. It forces 9600 baud and needs RTS asserted, which
  this board does not wire.
