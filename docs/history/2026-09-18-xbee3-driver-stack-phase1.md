# XBee 3 driver stack, phase 1: utils and board drivers

Date: 2026-09-18
Related plan: docs/plan/2026-09-18-xbee3-driver-stack.md

## Summary

Added the foundation layer of the XBee driver stack: two hardware-independent utilities
(`ring_buffer`, `byte_util`) and four board drivers (`xbee_power`, `xbee_reset`, `xbee_sleep`,
`xbee_uart`), registered them in the build, and introduced a host-side unit test folder with
tests for both utilities.

No service or application code exists yet, so nothing calls these modules and the firmware image
is unchanged. Phases 2 to 5 of the plan add the frame codec, the AT command table, the two
transports, the facade and the bring-up module.

## Motivation

The provisioning firmware has to talk to the XBee module, and the plan splits that work into
layers. This phase provides everything below the protocol: a byte FIFO the UART interrupt can
fill, big-endian and hexadecimal helpers that every frame and Command mode value needs, and the
four board control paths (supply, reset, sleep request, sleep status, serial bytes).

## Changes

| File | Change |
| --- | --- |
| `docs/plan/2026-09-18-xbee3-driver-stack.md` | New plan covering all five phases (committed separately in `2211cb4`) |
| `src/utils/inc/ring_buffer.h`, `src/utils/src/ring_buffer.c` | New: byte FIFO over caller storage, lock-free for one producer and one consumer, power-of-two capacity |
| `src/utils/inc/byte_util.h`, `src/utils/src/byte_util.c` | New: big-endian 16, 32 and 64-bit accessors; ASCII hexadecimal encode and decode; variable-width big-endian to 64-bit |
| `src/drivers/inc/xbee_power.h`, `src/drivers/src/xbee_power.c` | New: `XBEE_EN_GPIO` load switch, push-pull, module off after init |
| `src/drivers/inc/xbee_reset.h`, `xbee_reset_config.h`, `src/drivers/src/xbee_reset.c` | New: `XBEE_nRESET` open drain, assert, release and a non-blocking low pulse |
| `src/drivers/inc/xbee_sleep.h`, `src/drivers/src/xbee_sleep.c` | New: `XBEE_SLEEPRQ` output and `XBEE_nSLEEP_Status` input, polled |
| `src/drivers/inc/xbee_uart.h`, `xbee_uart_config.h`, `src/drivers/src/xbee_uart.c` | New: byte transport over the UARTDRV `XBEE` instance, double-buffered one-byte receive into the ring, single staged transmit, counters |
| `src/drivers/inc/.gitkeep`, `src/drivers/src/.gitkeep` | Removed: the folders now hold real files |
| `cmake_gcc/CMakeLists.txt` | Registered the six new `.c` files and the `../src/drivers/inc` include path, inside the permitted sections only |
| `test/CMakeLists.txt`, `test/test_util.h` | New: host test build and a minimal assertion harness |
| `test/test_ring_buffer.c`, `test/test_byte_util.c` | New: unit tests for the two utilities |
| `readme.md` | Documented the three new pins, the new modules and how to run the host tests |

Design points worth recording:

- **Receive path.** `UARTDRV_Receive()` only completes when its full byte count has arrived, and
  XBee replies are variable length, so the driver keeps two one-byte operations queued and pushes
  each completed byte into the ring. Verified in the SDK source
  (`platform/emdrv/uartdrv/src/uartdrv.c`, `ReceiveDmaComplete()` at line 697) that the user
  callback runs before the finished operation is dequeued and that UARTDRV starts the next queued
  operation immediately afterwards, which makes re-queueing from inside the callback the intended
  pattern.
- **Ring buffer concurrency.** One producer in interrupt context and one consumer in the super
  loop, so no critical section is needed: the push path only advances the head and the pop path
  only advances the tail, with a compiler barrier ordering the data access against the index
  publication. A full memory barrier is not needed on a single-core Cortex-M33.
- **Reset pin drive.** Open drain (`SL_GPIO_MODE_WIRED_AND`), so the MCU only ever pulls the reset
  line low and the module pull-up releases it. That is safe if a second reset source shares the
  net.
- **Counters.** The four UART counters are written from interrupt context and read from the super
  loop, so `xbee_uart_get_stats()` takes them as one snapshot inside `CORE_ENTER_ATOMIC()`.
  `xbee_uart_write()` claims the staging buffer inside the same kind of critical section, so two
  callers cannot both start filling it.

## Simplicity Studio changes (made by the user)

None as part of this change set. The three control pins this phase drives were added by the user
in Pin Tool earlier the same day and committed separately as `ef1987e`:

| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |
| Pin Tool | Custom pin name | `XBEE_nRESET` | not present | PB03, reserved |
| Pin Tool | Custom pin name | `XBEE_nSLEEP_Status` | not present | PB04, reserved |
| Pin Tool | Custom pin name | `XBEE_SLEEPRQ` | not present | PB05, reserved |

The application consumes the generated macros `XBEE_nRESET_GPIO_PORT/PIN`,
`XBEE_nSLEEP_Status_GPIO_PORT/PIN` and `XBEE_SLEEPRQ_GPIO_PORT/PIN` from `config/pin_config.h`
verbatim, including Pin Tool's mixed-case spelling.

## Verification

**Build only and host tests. Nothing in this phase has been run on hardware.**

- Target build: `cd cmake_gcc && cmake --workflow --preset project` succeeded with no warnings
  under `-Wall -Wextra -Os`. All six new sources compiled.
- Image size before and after, `arm-none-eabi-size` on `cmake_gcc/build/base/xbee_provision.out`:

  | | text | data | bss |
  | --- | --- | --- | --- |
  | Before and after | 24164 | 372 | 261772 |

  Unchanged, because nothing references the new modules yet and `--gc-sections` drops them. The
  ring and staging buffers cost RAM only once `xbee_uart_init()` is referenced.
- Host tests: `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir
  test/build --output-on-failure`. Both suites passed, 3101 checks for the ring buffer and 70 for
  the byte helpers. The ring buffer suite covers ordering, the reserved slot at capacity, index
  wrap over 1000 cycles, bulk reads across the wrap point, clear and every NULL path. The byte
  helper suite covers big-endian round trips, hexadecimal encoding and decoding including the
  optional `0x` prefix and odd digit counts, scalar conversion with overflow limits, and a full
  256-byte encode and decode round trip.
- Static review: every `sl_status_t` and `Ecode_t` return value is checked; no blocking wait
  anywhere; interrupt-shared state is `volatile`; multi-word shared state is read inside a core
  atomic section; every copy is bounded by an explicit length check.
- `git status` shows no Studio-owned file modified.

## Known limitations and follow-ups

- **Not tested on hardware.** The plan's phase 1 on-target checks are still outstanding: PA00 high
  after power on, a 10 ms low pulse on PB03, PB05 following the sleep request, PB04 reading high
  with a powered and awake module, and an optional PB00 to PB01 loopback to prove the receive path
  with no overruns.
- `XBEE_RESET_PULSE_MS`, 10 ms, is **unverified**. The minimum RESET low time is not in
  `docs/manuals/xbee_90002273_ref_manual.md`; it is in the XBee 3 Hardware Reference Manual, which
  is not in the repository. Recommendation: add that manual so the figure can be cited.
- `xbee_sleep_is_awake()` reads an undefined level while the module is unpowered, and the sleep
  lines only carry meaning when the module is configured for pin sleep. The facade in phase 5 is
  responsible for checking `SM`, `D8` and `D9` first.
- With no RTS or CTS flow control, a full receive ring loses bytes. The driver counts the loss as
  an overrun and the frame parser in phase 2 will resynchronise on the next start delimiter, but
  there is no way to ask the module to pause.
- The host test build depends on `SDK_PATH`, which it reads from the generated
  `cmake_gcc/xbee_provision.cmake`. That file is not in git, so a fresh clone must be generated in
  Simplicity Studio once, or the path passed explicitly.
