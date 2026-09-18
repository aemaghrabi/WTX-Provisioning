# XBee 3 driver stack, phase 3: API mode transport

Date: 2026-09-18
Related plan: docs/plan/2026-09-18-xbee3-driver-stack.md

## Summary

Added `xbee_api`, the transport that carries API frames: it assigns frame
identifiers, encodes and sends frames, parses the incoming byte stream, matches
responses to the requests that asked for them, times out the rest, and hands
everything unsolicited to a callback. It works in API mode 1 and API mode 2
alike.

Also added a host-side fake platform, a virtual clock and a byte pipe standing
in for the sleeptimer and the UART driver, so the transport's timing and
matching logic can be tested deterministically on the host. That was not in the
plan; the reasoning is below.

## Motivation

The codec knows how to build and read a frame but nothing about conversations.
Something has to decide which response belongs to which request, what happens
when no response arrives, and where a frame nobody asked for should go. That
logic is easy to get subtly wrong and impossible to check by reading, so it was
worth making testable.

## Changes

| File | Change |
| --- | --- |
| `src/services/inc/xbee_api.h`, `xbee_api_config.h`, `src/services/src/xbee_api.c` | New: request tracking, frame identifier allocation, dispatch, timeouts, counters |
| `test/fake_platform.h`, `test/fake_platform.c` | New: host stand-ins for the sleeptimer and `xbee_uart` |
| `test/test_xbee_api.c`, `test/CMakeLists.txt` | New test suite |
| `cmake_gcc/CMakeLists.txt` | Registered the new source |

Design points worth recording:

- **Requests belong to the caller.** There is no pool and no allocation. The
  transport keeps an array of pointers to requests that are in flight, so the
  number of concurrent requests is bounded by a configured constant rather than
  by memory the module owns.
- **A request object needs no initialisation.** Whether one is already in flight
  is answered by the transport's own tracking table, never by reading the
  request's fields. An earlier version checked the request's state field, which
  is undefined for a caller's fresh stack object; the host tests caught it
  immediately.
- **Responses are copied.** The parser buffer is reused by the next frame, so a
  response that must outlive the dispatch is copied into the request first and
  decoded against that copy. A response too large for the request's buffer fails
  the request rather than truncating, because a silently shortened `SH` or `SL`
  would be worse than a reported error.
- **A multi-response request ends at its timeout.** For a Network Discover the
  window closing is the documented end of the operation, not a failure, so the
  result is success as long as at least one node answered.
- **Late answers are counted.** A response that arrives after its request timed
  out is not written into a request the caller may already have reclaimed; it is
  counted as orphaned and passed to the unsolicited callback.

## A finding worth recording: 0x7E in a payload

While writing the tests, a response whose value happened to contain `0x7E` broke
framing in API mode 1. That is correct behaviour, not a defect: the manual states
that an unescaped start delimiter is always taken as the beginning of a new frame
and everything before it is discarded (lines 7193 to 7195), and that
distinguishing a `0x7E` in the data from the delimiter is the one real reason API
mode 2 exists (lines 7217 to 7222).

The practical consequence for this project is that API mode 1 is only safe when
every byte the module sends back is under our control. Reading known parameters
is fine. Carrying arbitrary payloads is not. A test now pins the difference down
in both modes so the choice is explicit rather than accidental.

## Why the host fakes were added

The plan listed host tests only for the codec, the command table and the Command
mode line parser, because the transports depend on a clock and a serial port.
Both dependencies turned out to be small and easy to stand in for: the sleeptimer
header compiles on the host, and `xbee_uart` has a narrow interface. Roughly a
hundred lines of fakes made timeout behaviour, tick counter wrap, frame
identifier reuse, response matching and stream resynchronisation testable without
hardware. The alternative was discovering those on the board, one power cycle at
a time. The plan has been updated.

## Simplicity Studio changes (made by the user)

None. This phase is application code only.

## Verification

**Build and host tests. Nothing in this phase has been run on hardware.**

- Target build clean under `-Wall -Wextra -Os`.
- `xbee_api` object: 1353 bytes of text, 855 bytes of static RAM, the latter
  dominated by the parser buffer and the staging buffer for an outgoing frame.
- Host tests: `test_xbee_api`, 186 checks, all passing. It covers a query sent
  and answered, identifier allocation across concurrent requests, suppressing the
  answer with identifier 0, a request that expects no answer at all, timeouts
  including one spanning the 32-bit tick counter wrap, a multi-response
  discovery ending both with and without answers, unsolicited frames including an
  undocumented type, API mode 2 escaping in both directions, a response too large
  for the request buffer, aborting every pending request, the tracking table
  filling up, transmit status matching, noise and corrupt frames in the stream,
  a frame arriving one byte at a time, and the start delimiter in a payload.
- All six suites pass together: 3101, 70, 340, 23298, 186 and 403 checks.
- Static review: no blocking waits; deadlines compared as signed differences so
  they survive the counter wrap; every copy bounded by an explicit length check.
- `git status` shows no Studio-owned file modified.

## Known limitations and follow-ups

- **Not tested on hardware.** No frame has yet been exchanged with a real module.
- API mode 1 cannot carry arbitrary payloads safely, as described above. The
  facade reports the detected mode so the application can decide.
- A response longer than the request's buffer fails the request. The buffer is a
  configured constant; a caller that needs a long value, such as the Version Long
  text, must raise it.
- The transport does not retry. A timed-out request is reported to the caller,
  which decides whether to try again.
