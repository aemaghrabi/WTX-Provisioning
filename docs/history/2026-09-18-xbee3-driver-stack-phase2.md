# XBee 3 driver stack, phase 2: frame codec and AT command table

Date: 2026-09-18
Related plan: docs/plan/2026-09-18-xbee3-driver-stack.md

## Summary

Added the protocol layer of the XBee driver stack: `xbee_frame`, a complete API frame codec with a
streaming parser that handles both API mode 1 and API mode 2, and `xbee_at_table`, a constant
description of all 147 AT commands the 802.15.4 manual documents. Both are hardware independent
and covered by host unit tests, including the manual's own worked examples.

Still nothing calls these modules, so the firmware image is unchanged. Phases 3 to 5 add the two
transports, the facade and the bring-up module.

## Motivation

Everything above this layer needs to speak the module's protocol: the transports build and parse
frames, and the facade needs to know what each AT command expects before it sends anything. Putting
both in hardware-independent modules means the trickiest logic in the stack, escaping, checksums,
frame layouts and parameter ranges, is fully testable on the host rather than only on the board.

## Changes

| File | Change |
| --- | --- |
| `src/services/inc/xbee_frame.h`, `xbee_frame_config.h`, `src/services/src/xbee_frame.c` | New: 26 frame structures, defaults builders for the 10 host-to-module frames, encode with optional escaping, decode, checksum helpers, streaming parser, status-to-text helpers |
| `src/services/inc/xbee_at_table.h`, `src/services/src/xbee_at_table.c` | New: named identifier for every command, 147-entry table with type, range, default and access flags, lookup, validation and enumeration |
| `cmake_gcc/CMakeLists.txt` | Registered both sources and the `../src/services/inc` include path |
| `test/test_xbee_frame.c`, `test/test_xbee_at_table.c`, `test/CMakeLists.txt` | New test suites |
| `src/services/inc/.gitkeep`, `src/services/src/.gitkeep` | Removed: the folders now hold real files |
| `docs/plan/2026-09-18-xbee3-driver-stack.md` | Recorded the lookup decision and the manual errors found during implementation |

Design points worth recording:

- **Frame scope.** The manual in `docs/manuals/` is the 802.15.4 guide, which documents exactly 26
  frames. Zigbee and DigiMesh frames such as `0x21`, `0x24`, `0x94` and `0xA0` do not exist for this
  product. Any undocumented frame type decodes into a raw variant and can be encoded back
  unchanged, so a future firmware variant does not need a codec change to pass through.
- **Encoding without a scratch buffer.** The encoder computes the frame data length first, then
  writes the delimiter, length, fields and checksum straight into the caller's buffer through a
  small writer that escapes as it goes and accumulates the checksum. The length and the checksum are
  escaped but are not folded into the checksum, and the start delimiter is never escaped, exactly as
  the manual requires at lines 7199 to 7231.
- **Unified I/O samples.** The legacy frames `0x82` and `0x83` carry one 16-bit mask whose low nine
  bits are digital lines and whose bits 9 to 12 are the analog channels, while `0x92` carries
  separate digital and analog masks. The decoder splits the legacy mask into the same two fields, so
  a consumer handles one shape whatever `AO` is set to.
- **Lookup by linear scan.** Ordering the command table by identifier would scramble the categorical
  grouping and make it much harder to review against the manual. The scan costs under ten
  microseconds for all 147 entries and runs a few dozen times per provisioning session. A test
  enforces that every identifier is unique. This deviates from the plan, which called for a binary
  search; the plan has been updated with the reason.
- **Validation is advisory, not authoritative.** The table lets a transport reject an impossible
  request before it reaches the module, but the module remains the authority: two documented ranges
  have gaps that are not enforced, and a few commands silently adjust the value they store.

## Manual errors found and how they were resolved

These are recorded because the code deliberately departs from what the manual prints.

| Location | What the manual says | What was implemented |
| --- | --- | --- |
| Frame `0x8B`, lines 8840 to 8843 | Offset 7 appears twice, for the retry count and a reserved byte | Delivery status at offset 8, discovery status at offset 9, seven bytes of frame data |
| Frame `0x92` example, lines 9118 to 9148 | Checksum `0xE8`, and a field table showing reserved as `0x87AC` | The printed checksum does not verify against the example's own hex, under either reading of the reserved field. The field layout is consistent and is what the decoder implements; the test checks the layout and not the printed checksum |
| Frame `0x88` example, lines 8622 to 8630 | Field table shows frame identifier `0xA1`, hex string carries `0x01` | The hex is self-consistent with its checksum, so it is used |
| `NO`, lines 4986 to 5004 | Range 0 to 1, while listing bits `0x01`, `0x02` and `0x04` | Range 0 to 7, taking the bit field as authoritative |
| `SM`, lines 5655 to 5676 | Range 0 to 5, while the value table lists 6 | Range 0 to 6 |
| `*X`, line 5537 | Verifier bytes 54 to 95 | Bytes 64 to 95; all four slices are 32 bytes |
| `DM` and `LT` | Ranges with a documented gap, 0 and 4 to `0x1F`, and 0 and `0x14` to `0xFF` | Outer bounds stored, gaps noted in comments but not enforced |

## Simplicity Studio changes (made by the user)

None. This phase is application code only.

## Verification

**Build and host tests. Nothing in this phase has been run on hardware.**

- Target build: `cd cmake_gcc && cmake --workflow --preset project` succeeded with no warnings
  under `-Wall -Wextra -Os`.
- Image size is unchanged at 24164 text, 372 data, 261772 bss, because nothing references the new
  modules and `--gc-sections` drops them. Per-module object sizes, from `arm-none-eabi-size`:

  | Module | text | bss |
  | --- | --- | --- |
  | `ring_buffer` | 240 | 0 |
  | `byte_util` | 652 | 0 |
  | `xbee_power` | 86 | 0 |
  | `xbee_sleep` | 152 | 0 |
  | `xbee_reset` | 214 | 5 |
  | `xbee_uart` | 588 | 800 |
  | `xbee_at_table` | 3252 | 0 |
  | `xbee_frame` | 6102 | 0 |

  The command table is close to the 2.4 kB the plan estimated: 147 entries of 20 bytes is 2940
  bytes, plus about 310 bytes of lookup and validation code. The codec is at the upper end of the
  plan's 4 to 6 kB estimate, with the status-to-text tables included.
- Host tests: four suites, all passing.

  | Suite | Checks |
  | --- | --- |
  | `test_ring_buffer` | 3101 |
  | `test_byte_util` | 70 |
  | `test_xbee_frame` | 340 |
  | `test_xbee_at_table` | 23298 |

  The codec suite exercises the manual's checksum example at lines 7285 to 7314, both Local AT
  Command examples at lines 7519 to 7534, the escaping example at lines 7232 to 7245 in both API
  modes, the Local AT Command Response examples, the Modem Status example, the I/O Sample field
  layout, an encode and decode round trip of all ten host-to-module frames through the streaming
  parser in both API modes, parser resynchronisation on a stray start delimiter, checksum rejection,
  an announced length beyond the buffer, and every documented field limit. The table suite checks
  the entry count, identifier uniqueness across all pairs, that every category is represented,
  the documented values of spot-checked commands, the access flags, and validation of both reads
  and writes.
- Static review: every `sl_status_t` return value is checked; no dynamic allocation; every decode
  path bounds-checks before reading a field; the parser never writes past its buffer, and it keeps
  consuming an over-long frame so the byte stream stays aligned.
- `git status` shows no Studio-owned file modified.

## Known limitations and follow-ups

- **Not tested on hardware.** Nothing has yet exchanged a frame with a real module.
- Frame `0x8B` is implemented from a corrected reading of a table the manual prints with a
  duplicated offset. It should be confirmed against a real transmit status during phase 3 or 5.
- The `0x92` example in the manual cannot be used as an end-to-end check because its checksum does
  not verify. Only its field layout is tested.
- Two documented ranges have gaps that the table does not enforce, `DM` and `LT`. The module
  rejects the gap values itself.
- `xbee_at_table` describes 32-bit bounds, so the three 64-bit commands, `US`, `IA` and `D%`, are
  validated by width only, not by value.
- The command names behind `XBEE_AT_TABLE_NAMES` are compiled out by default. Turning them on adds
  roughly 2 kB of flash.
- Stale object files from earlier work are sitting in the gitignored build directory, for sources
  that no longer exist (`src/app/src/log_demo.c`, `src/app/src/xbee_provision.c`,
  `src/services/src/xbee_at.c`). They are not linked into the image. A clean build directory would
  remove them; nothing was deleted as part of this change.
