# XBee 3 (802.15.4) driver stack: utils, drivers, services

Status: In progress
Date: 2026-09-18
Related history: docs/history/2026-09-18-xbee3-driver-stack-phase1.md

## Goal

Provide a layered driver stack that hides both the UART transport and the XBee protocol from the
application, so that provisioning logic can read and write every XBee parameter and exchange every
documented API frame without knowing which serial mode the module is in.

Measurable outcomes:

- The same application code works with the module in API mode 1 (`AP=1`, unescaped), API mode 2
  (`AP=2`, escaped) and Transparent mode (`AP=0`, parameters reached through `+++` Command mode).
  No recompilation and no `#if` in application code.
- Every AT command documented in `docs/manuals/xbee_90002273_ref_manual.md` is reachable by name,
  with its type, range, default and access flags known to the stack.
- Every one of the 26 API frames documented in that manual can be encoded and decoded, with a raw
  pass-through for frame types the manual does not cover.
- Each frame is built from a struct whose fields carry the manual's defaults and can be overridden
  individually.
- The module's power, reset and sleep control lines are driven through dedicated drivers.
- `app_process_action()` stays non-blocking: every wait is a state machine with a timeout.

## Background and references

All references are to `docs/manuals/xbee_90002273_ref_manual.md`, the Digi XBee 3 802.15.4 RF
Module User Guide, part number 90002273 R. The module is **802.15.4**, not Zigbee or DigiMesh, so
Zigbee-only frames (`0x21`, `0x24`, `0x2A`, `0x94`, `0xA0`, `0xA3`, `0xA5`, `0xA7`, `0xAF`, `0xB0`)
do not exist here and are out of scope.

| Topic | Lines | What it establishes |
| --- | --- | --- |
| Product identity | 1-70, 685-710 | XBee 3 802.15.4, firmware `0x20xx`, EFR32 based |
| Transparent mode, packetization | 2997-3020 | `RO`, `NP`, the `GT + CC + GT` sequence |
| Command mode entry and exit | 3044-3124 | `GT` silence, `+++`, `GT` silence, reply `OK\r`; exit with `ATCN` or `CT` timeout; `OK<cr>` / `ERROR<cr>`; comma-separated commands; hex with or without `0x` |
| `CC`, `CT`, `GT` | 6139-6179 | defaults `0x2B`, `0x64` (10 s, x100 ms), `0x3E8` (1 s, x1 ms) |
| `AP` | 5996-6013, 7152-7170 | 0 Transparent, 1 API, 2 API escaped, 4 MicroPython |
| `AO` | 6014-6037 | default **2**, which emits legacy `0x80`/`0x81`/`0x82`/`0x83`; `AO=0` gives `0x90`/`0x92` |
| API frame format | 7171-7266 | `0x7E`, 16-bit big-endian length of the frame data, frame data, checksum; multi-byte fields big-endian |
| Escaping, `AP=2` | 7188-7245 | escape `0x7E`, `0x7D`, `0x11`, `0x13` as `0x7D` followed by the byte XOR `0x20`; the start delimiter is never escaped; length and checksum are computed on unescaped data; an unescaped `0x7E` restarts the frame |
| Checksum | 7268-7314 | `0xFF - (sum of frame data & 0xFF)`; verification sums the checksum in and expects `0xFF`; worked example `7E 00 08 08 01 4E 49 58 42 45 45 3B` |
| Frame descriptions | 7315-9494 | the 26 frame layouts, table of contents at 7317-7343 |
| AT commands | 4812-7140 | 24 categories, about 147 commands |
| Delivery status | 8674-8731, 8844-8884 | `0x89` and `0x8B` status code tables |
| Modem status | 8762-8802, 4338-4352 | `0x8A` codes, plus the 802.15.4 subset |
| AT response status | 8613-8620, 9187-9194, 2572-2588 | 0 OK, 1 ERROR, 2 invalid command, 3 invalid parameter; remote adds 4, `0x0B`, `0x0C`, `0x0D` |
| `ND` response format | 5005-5052 | one `0x88` per node in API mode; field-per-line text in Command mode, ended by a second carriage return after `NT` |
| Maximum payload | 4465-4560 | 116 bytes absolute maximum; `NP` reports the current value |
| `BD` | 6060-6089 | standard values 0 to `0x0A`, default 3 = 9600 baud |
| Serial buffers | 3141-3219 | on overflow the module discards all incoming data; no byte sizes are given |
| `WR` | 7089-7099 | 10 000 erase and write cycles; do not send further characters until `OK` |
| `FR` | 7063-7070 | replies `OK` immediately, resets 100 ms later |
| `FS` | 5808-5916 | Command mode only; errors are `E<CODE> <text>` lines |
| Sleep modes | 4703-4718 | `SM`: 0 none, 1 pin sleep, 4 cyclic, 5 cyclic with pin wake, 6 MicroPython |
| Pin sleep, `SM=1` | 4719-4729 | needs `D8=1`; SLEEP_RQ is level activated, asserted means sleep, de-asserted means wake; ready when CTS goes low; de-assert at least two byte times after CTS low |
| Cyclic with pin wake, `SM=5` | 4748-4757 | a high to low transition on SLEEP_RQ wakes; the module stays awake while the line is low; minimum wake time `ST` |
| Sleep pins | 4780-4788 | DTR/SLEEP_RQ: for `SM=1`, high sleeps and low wakes. ON_SLEEP: low means asleep, high means awake |
| Sleep conditions | 4789-4811 | the module will not sleep while in or entering Command mode, while processing AT commands, or with serial data queued |
| `D8`, `D9` | 6440-6471 | `D8` default 1 is DTR/Sleep_Request; `D9` default 1 is the ON/SLEEP indicator |
| `SM`, `SP`, `ST`, `SO` | 5655-5748 | sleep parameter ranges and defaults |
| RESET pin | not in this guide | the electrical data and the minimum low time are only in the XBee 3 hardware reference manual, which is not in `docs/manuals/` |

SDK facts, verified in the Simplicity SDK 2025.6.2 tree used by this project
(`/Users/ahmedel-maghrabi/.silabs/slt/installs/conan/p/simpleb526998f4a4d/p`):

- `platform/emdrv/uartdrv/src/uartdrv.c`, `ReceiveDmaComplete()` at line 697: the user callback
  runs inside `CORE_ENTER_ATOMIC()`, before the finished operation is dequeued; the next queued
  operation is started immediately afterwards. Re-queueing from inside the callback is the
  intended pattern, as the comment in `UARTDRV_Receive()` states ("else: started by
  ReceiveDmaComplete").
- `UARTDRV_Callback_t` is `(handle, Ecode_t transferStatus, uint8_t *data, UARTDRV_Count_t count)`.
- `platform/common/inc/sl_status.h` includes only `<stdint.h>`, so modules that depend on nothing
  else can be compiled on the host for unit tests.
- `sl_gpio.h` offers `sl_gpio_set_pin_mode()`, `sl_gpio_set_pin()`, `sl_gpio_clear_pin()`,
  `sl_gpio_get_pin_output()` and `sl_gpio_get_pin_input()`.
  `sl_device_gpio.h` defines `SL_GPIO_MODE_INPUT`, `SL_GPIO_MODE_INPUT_PULL`,
  `SL_GPIO_MODE_PUSH_PULL`, `SL_GPIO_MODE_WIRED_AND` and `SL_GPIO_MODE_WIRED_AND_PULLUP`.
- Installed components cover everything needed: `uartdrv_eusart` instance `XBEE`, `sl_gpio`,
  `sl_sleeptimer` on SYSRTC, `sl_core`, DMADRV. There is no power manager, so the device stays in
  EM0, and there is no queue, timer or circular buffer component.
- `config/sl_uartdrv_eusart_XBEE_config.h`: 9600 baud, 8N1, no flow control, RX and TX operation
  queue depth 6. Those are operation counts, not byte counts.
- `config/pin_config.h` custom pin names, generated by Pin Tool and used read-only here:
  `XBEE_EN_GPIO_PORT/PIN` on PA00, `XBEE_nRESET_GPIO_PORT/PIN` on PB03,
  `XBEE_nSLEEP_Status_GPIO_PORT/PIN` on PB04, `XBEE_SLEEPRQ_GPIO_PORT/PIN` on PB05. The macro
  names keep Pin Tool's mixed case and are used verbatim. The unnamed custom pin on PA01 is SWCLK
  and is unrelated to the XBee.

Decisions taken with the user on 2026-09-18:

| Topic | Decision |
| --- | --- |
| Mode selection | The application passes a mode to `xbee_init()`. `XBEE_MODE_AUTO` probes: an API query first, then `+++`. `xbee_set_mode()` switches at runtime. |
| Frame scope | The 26 frames of the 802.15.4 manual, 10 host to module and 16 module to host, plus a raw pass-through for other frame types. No Zigbee or DigiMesh frames. |
| Receive path | Two 1-byte `UARTDRV_Receive` operations are always queued; each callback pushes the byte into a ring buffer and re-queues itself. |
| Builder style | One struct per frame. A `_defaults()` function fills the manual's defaults, the caller overrides the fields it needs, then the frame is encoded. |
| Control pins | Dedicated drivers for reset, sleep request and sleep status. ON_SLEEP is polled on request, with no interrupt. The facade adds hardware reset and sleep enter and exit helpers. |
| Reset drive | Open drain, `SL_GPIO_MODE_WIRED_AND`, relying on the module pull-up. Low pulse `XBEE_RESET_PULSE_MS`, default 10 ms, unverified. |
| Tests | Host-side unit tests in a new top-level `test/` folder for the hardware-independent modules. |
| Delivery | Five phases. Each one builds, and each one gets its own history entry. |

## Design

### Layering

Layer rules come from `docs/plan/2026-09-17-project-folder-structure.md`.

```
app.c
  -> src/app/xbee_bringup             phase 5: power on, probe, read SH, SL, VR, HV, log
     -> src/services/xbee             facade: mode-independent API, AUTO probe, mode switch
        -> src/services/xbee_api      API transport: frame IDs, requests, timeouts, dispatch
        -> src/services/xbee_cmd_mode Command mode transport: +++, ASCII lines, CT tracking
        -> src/services/xbee_frame    pure codec: 26 frame structs, encode, decode, stream parser
        -> src/services/xbee_at_table constant table of every AT command with type and flags
           -> src/drivers/xbee_uart   UARTDRV wrapper: receive ring, transmit buffer, counters
           -> src/drivers/xbee_power  XBEE_EN_GPIO load switch
           -> src/drivers/xbee_reset  XBEE_nRESET, open drain
           -> src/drivers/xbee_sleep  XBEE_SLEEPRQ output, XBEE_nSLEEP_Status input
              -> SDK: UARTDRV, sl_gpio, sl_sleeptimer, sl_core
src/utils: ring_buffer, byte_util; app_log already exists
```

`xbee_frame`, `xbee_at_table`, `ring_buffer`, `byte_util` and the Command-mode line parser depend
on nothing beyond `sl_status.h` and the C standard library, so they compile on the host.

House style for every new module: snake_case file names, `MODULE_H` include guards, public symbols
prefixed with the module name, Doxygen file headers, 2-space indentation, `static` for file-local
symbols, fixed-width integer types, no dynamic allocation, no icons in any string. A module with
tunables gets an `#ifndef`-guarded `<module>_config.h` beside its public header, following
`src/utils/inc/app_log_config.h`.

### utils: `ring_buffer`

`src/utils/inc/ring_buffer.h`, `src/utils/src/ring_buffer.c`.

- Byte FIFO over caller-provided storage. The capacity must be a power of two, checked at init.
- Single producer in interrupt context, single consumer in the super loop. `volatile` head and
  tail indices: push writes the byte, then publishes the new head; pop reads the byte, then
  publishes the new tail. That ordering makes the single-producer, single-consumer case safe
  without disabling interrupts.
- API: `ring_buffer_init()`, `ring_buffer_push()` returning `SL_STATUS_FULL` when full,
  `ring_buffer_pop()` returning `SL_STATUS_EMPTY` when empty, `ring_buffer_read()` for bulk
  drain, `ring_buffer_count()`, `ring_buffer_free()`, `ring_buffer_clear()`.

### utils: `byte_util`

`src/utils/inc/byte_util.h`, `src/utils/src/byte_util.c`.

- Big-endian accessors, which every API frame field needs: `byte_util_read_be16/32/64()` and
  `byte_util_write_be16/32/64()`.
- ASCII hexadecimal helpers for Command mode: `byte_util_hex_encode()` producing uppercase digits
  with no `0x` prefix, `byte_util_hex_decode()` accepting an optional `0x` prefix and an odd digit
  count, `byte_util_hex_to_u32()` and `byte_util_hex_to_u64()`.

### drivers: `xbee_power`

`src/drivers/inc/xbee_power.h`, `src/drivers/src/xbee_power.c`.

- Drives `XBEE_EN_GPIO` on PA00, the load switch that supplies the module.
- `xbee_power_init()` configures push-pull with the output low, so the module starts unpowered.
- `xbee_power_on()`, `xbee_power_off()`, `xbee_power_is_on()`. No delays inside: the facade owns
  the settle timer.

### drivers: `xbee_reset`

`src/drivers/inc/xbee_reset.h`, `src/drivers/inc/xbee_reset_config.h`,
`src/drivers/src/xbee_reset.c`.

- Drives `XBEE_nRESET` on PB03 as open drain, `SL_GPIO_MODE_WIRED_AND`, released at init so the
  module pull-up holds the line high. Open drain because the line may also be driven by the module
  or by a programming header.
- `xbee_reset_assert()` pulls low, `xbee_reset_release()` lets go, `xbee_reset_is_asserted()`.
- `xbee_reset_pulse_start()` and `xbee_reset_pulse_process()` implement a non-blocking low pulse:
  the start call asserts and records a sleeptimer tick, the process call releases the line once
  `XBEE_RESET_PULSE_MS` has elapsed. The process call returns `SL_STATUS_IN_PROGRESS` while the
  line is still low and `SL_STATUS_OK` afterwards.
- `XBEE_RESET_PULSE_MS` defaults to 10 and is **unverified**: the minimum RESET low time is only
  in the XBee 3 hardware reference manual, which is not in the repository.
- Consequences the facade must handle: the module reboots, emits Modem Status `0x8A` with status
  `0x00` in API mode, and leaves Command mode. Pending API requests are failed with
  `SL_STATUS_ABORT` and the frame parser is reset.

### drivers: `xbee_sleep`

`src/drivers/inc/xbee_sleep.h`, `src/drivers/src/xbee_sleep.c`.

- `XBEE_SLEEPRQ` on PB05 is the module's DTR/SLEEP_RQ input: push-pull output, initialised low,
  which is de-asserted and means awake. `xbee_sleep_request(true)` drives it high to request
  sleep, `xbee_sleep_request(false)` drives it low to wake.
  `xbee_sleep_request_is_asserted()` reports the current output level.
- `XBEE_nSLEEP_Status` on PB04 is the module's ON_SLEEP output: configured as a plain input, since
  the module drives it. `xbee_sleep_is_awake()` reads it, high meaning awake and low meaning
  asleep, per manual line 4787.
- The status line only carries meaning when the module has `D9=1` and a pin-based sleep mode
  (`SM=1` or `SM=5`, with `D8=1`). The driver cannot know the module's configuration, so the
  facade checks `SM`, `D8` and `D9` before relying on the pin.

### drivers: `xbee_uart`

`src/drivers/inc/xbee_uart.h`, `src/drivers/inc/xbee_uart_config.h`,
`src/drivers/src/xbee_uart.c`.

- Wraps `sl_uartdrv_eusart_XBEE_handle` from `autogen/sl_uartdrv_instances.h`. It does not
  initialise the peripheral: `sl_uartdrv_init_instances()` already ran inside `sl_driver_init()`.
- Receive: two static one-byte slots. `xbee_uart_init()` queues a `UARTDRV_Receive()` for each.
  The completion callback runs in interrupt context: on success it pushes the byte into the
  receive ring, otherwise it counts an error; if the push fails it counts an overrun; either way
  it re-queues the same slot. All counters are `volatile`. Nothing logs from the callback, since
  `app_log` refuses interrupt context anyway.
- Transmit: one static staging buffer and a `volatile` busy flag. `xbee_uart_write()` returns
  `SL_STATUS_BUSY` while a transmit is in flight and `SL_STATUS_WOULD_OVERFLOW` when the data does
  not fit; otherwise it copies and calls `UARTDRV_Transmit()`. The transmit callback clears the
  flag. `xbee_uart_tx_busy()` exposes it to the services.
- Consumer API: `xbee_uart_rx_available()`, `xbee_uart_read()`, `xbee_uart_rx_flush()`,
  `xbee_uart_get_stats()` reporting overruns, errors and byte counts.
- `xbee_uart_deinit()` aborts all operations, which is needed before the module is powered off so
  that no receive operation is left pending on a dead line.
- Configuration: `XBEE_UART_RX_RING_SIZE` 512, `XBEE_UART_TX_BUF_SIZE` 256.
- Timing: at 9600 baud, 8N1, one byte takes 1.04 ms and a 140-byte frame about 146 ms. The gap
  between two one-byte operations is handled inside a single LDMA interrupt, far shorter than one
  byte time, and the EUSART has a receive FIFO, so the double-buffered scheme loses no bytes.

### services: `xbee_frame`

`src/services/inc/xbee_frame.h`, `src/services/inc/xbee_frame_config.h`,
`src/services/src/xbee_frame.c`. Pure codec, no SDK dependency beyond `sl_status.h`.

- Protocol constants: start delimiter `0x7E`, escape `0x7D`, XON `0x11`, XOFF `0x13`, escape XOR
  `0x20`, broadcast and unknown addresses.
- `xbee_frame_type_t` carries the 26 documented identifiers. Host to module: `0x00`, `0x01`,
  `0x08`, `0x09`, `0x10`, `0x11`, `0x17`, `0x2C`, `0x2D`, `0x2E`. Module to host: `0x80`, `0x81`,
  `0x82`, `0x83`, `0x88`, `0x89`, `0x8A`, `0x8B`, `0x90`, `0x91`, `0x92`, `0x97`, `0x98`, `0xAC`,
  `0xAD`, `0xAE`.
- One struct per frame with the manual's field names. Variable-length payloads are a pointer and a
  length, pointing at caller memory when encoding and into the parser buffer when decoding. Option
  bit masks are named constants with the manual's line numbers in comments. Status values get
  enumerations: AT command status, delivery status, modem status, secure session status and
  extended modem status, each with an optional string helper behind
  `XBEE_FRAME_STATUS_STRINGS`.
- A tagged union `xbee_frame_t` holds the frame type plus the per-frame struct, with a raw variant
  for undocumented types.
- `xbee_frame_<name>_defaults()` for every host-to-module frame fills the manual's defaults:
  broadcast 64-bit destination, reserved 16-bit field `0xFFFE`, broadcast radius 0, options 0,
  endpoints `0xE8`, cluster `0x0011`, profile `0xC105`, relay interface 0, secure timeout 0.
- `xbee_frame_encode()` builds the delimiter, the length, the frame data and the checksum, and
  applies `AP=2` escaping to everything after the delimiter when asked. It returns
  `SL_STATUS_WOULD_OVERFLOW` when the output does not fit and `SL_STATUS_INVALID_PARAMETER` on
  field violations such as a payload above 116 bytes, a password above 64 characters or a node
  identifier above 20 characters.
- `xbee_frame_decode()` takes unescaped frame data, without the delimiter, the length or the
  checksum. An unknown type decodes into the raw variant and still returns `SL_STATUS_OK`; a
  truncated known frame returns `SL_STATUS_INVALID_RANGE`.
- `xbee_frame_checksum()` and `xbee_frame_checksum_ok()` implement the manual's arithmetic.
- The streaming parser `xbee_frame_parser_t` walks the states wait-for-start, length high byte,
  length low byte, data and checksum. It returns `SL_STATUS_IN_PROGRESS`, `SL_STATUS_OK` on a
  complete frame, `SL_STATUS_INVALID_SIGNATURE` on a checksum mismatch and
  `SL_STATUS_WOULD_OVERFLOW` when the announced length exceeds the buffer. An unescaped `0x7E` in
  any state restarts the frame, per manual line 7193. In escaped mode a `0x7D` arms the XOR for
  the next byte. Dropped frames are counted.
- Configuration: `XBEE_FRAME_MAX_DATA_LEN` 256. The largest documented frame is `0x11` with a
  20-byte header and a 116-byte payload, 136 bytes in total; the rest is margin for `VL` and `ND`
  text.

### services: `xbee_at_table`

`src/services/inc/xbee_at_table.h`, `src/services/src/xbee_at_table.c`.

- Command identifiers are the two ASCII characters packed into a 16-bit value, with one named
  constant per command, including the punctuation commands `*S`, `$S`, `%V`, `R?`, `D%` and `!C`.
- Each entry records the identifier, the manual category, a value type (unsigned 8, 16, 32 or 64
  bit, bitmap, string, byte array, executable, subcommand, multi-line), access flags (read only,
  write only, local only, volatile, Command mode only, needs write and reset, multiple responses),
  the maximum length, the range and the default. Entries cite their manual line range in a
  trailing comment. Optional human-readable names sit behind `XBEE_AT_TABLE_NAMES`, which defaults
  to 0 because the strings cost about 2 kB of flash.
- Coverage is every command in manual lines 4812 to 7140, about 147 entries over the 24
  categories, including the documented quirks: `KY` is write only, `SH`, `SL`, `NP`, `DB`, `VR`,
  `HV`, `%V`, `TP` and `CK` are read only, `FS` and `FK` have Command mode and local-only rules,
  `%F`, `!C` and `R1` cannot be sent remotely, `CA` needs a write and a reset, and `ND`, `AS` and
  `ED` produce multiple responses.
- API: `xbee_at_table_find()` by identifier using a binary search over a sorted table,
  `xbee_at_table_validate_set()` returning `SL_STATUS_PERMISSION` for a read-only command,
  `SL_STATUS_INVALID_RANGE` out of range and `SL_STATUS_INVALID_PARAMETER` on a wrong length,
  `xbee_at_table_validate_get()` rejecting write-only and executable commands, and
  `xbee_at_table_count()` with `xbee_at_table_at()` for enumeration.

### services: `xbee_api`

`src/services/inc/xbee_api.h`, `src/services/src/xbee_api.c`.

- Owns one frame parser over a static buffer and a small table of pending requests,
  `XBEE_API_MAX_PENDING` defaulting to 2. A request records its state, frame identifier, the frame
  type it expects, whether several responses are expected, its deadline in sleeptimer ticks, the
  decoded response and its backing buffer, and an optional completion callback.
- `xbee_api_send()` assigns the next free frame identifier, wrapping through 1 to 255 and never
  reusing one that is in flight, writes it into the frame, encodes into a staging buffer and hands
  the bytes to `xbee_uart_write()`. It returns `SL_STATUS_BUSY` when the UART or the request table
  is full. A frame identifier of 0 set by the caller means no response is expected.
- `xbee_api_process()` drains the UART ring into the parser, decodes each complete frame, matches
  responses to pending requests by type and identifier, and routes everything else to the
  unsolicited callback: modem status, received data in both the modern and legacy families, I/O
  samples, extended modem status, relay output and raw frames. It then expires overdue requests.
  For a multi-response request, such as `ND`, the timeout is the normal end of the operation.
- Timeouts: 1000 ms for a local AT command, 3000 ms for a remote command and for a transmit
  status. `ND` uses `NT` plus a margin, which the facade can read first.
- Timing compares sleeptimer ticks inside `process()`. No sleeptimer callbacks are registered, so
  the UART callback remains the only interrupt-context code in the stack.

### services: `xbee_cmd_mode`

`src/services/inc/xbee_cmd_mode.h`, `src/services/src/xbee_cmd_mode.c`. The line classifier is a
pure function so it can be unit tested on the host.

- Runtime parameters mirror the module: command character, guard time and command mode timeout,
  all updated by the facade once it has read `CC`, `GT` and `CT`.
- Entry is a state machine: wait for a guard time with no transmission, send the command character
  three times, wait for `OK\r` within the guard time plus a margin, then become active. Any
  transmission during the guard restarts it. Failure returns to idle with `SL_STATUS_TIMEOUT`.
- While active, a command is sent as `AT`, the two characters, an optional value and a carriage
  return. Replies are collected until a carriage return, with a one second timeout. Multi-line
  commands such as `ND`, `AS`, `ED` and `VL` collect lines until an empty line or the timeout.
  A reply is classified as `OK`, `ERROR`, a file system error line beginning with an uppercase `E`
  (manual lines 5820 to 5826), or a value line. Values are kept as text and converted on demand.
- Every accepted command refreshes the local command mode deadline. If it lapses, the state falls
  back to idle, because the module has left Command mode on its own.
- `xbee_cmd_mode_exit()` sends `ATCN` and waits for `OK`.
- When not active, received bytes are delivered to a data callback, which is the Transparent mode
  receive path, and `xbee_cmd_mode_send_data()` writes raw bytes for the module to packetize
  according to `RO` and `NP`.

### services: `xbee` facade

`src/services/inc/xbee.h`, `src/services/inc/xbee_config.h`, `src/services/src/xbee.c`.

- Configuration: requested mode, whether to power cycle at init, the settle time after power on
  (default 1000 ms) and the probe timeout per step (default 500 ms).
- Lifecycle: `xbee_init()`, `xbee_process()` which drives the active transport and the facade
  state machine, and `xbee_deinit()`.
- Facade states: off, power settle, probe API, probe Command mode, read mode parameters (`AP`,
  `GT`, `CC`, `CT`, `AO`, `NP`, `VR`), then ready or failed. Accessors report the state, the
  detected mode and the cached parameters.
- The AUTO probe sends one `0x08` query for `AP` with frame identifier 1. Neither that frame nor
  its response contains a byte that needs escaping, so it is valid in both `AP=1` and `AP=2`, and
  the returned value selects escaping. If no reply arrives within the probe timeout, the facade
  tries `+++`. If Command mode succeeds, it reads `AP`: 0 confirms Transparent mode, while 1 or 2
  means the module is in API mode but did not answer, which is reported as a failure rather than
  guessed. Worst case: 1 s settle, 0.5 s API probe, about 2.1 s for Command mode entry, then the
  parameter reads.
- Mode-independent parameter access routes to whichever transport is active: get, set, set from an
  unsigned value, execute, and queued set. Every call is validated against `xbee_at_table` first.
  In Command mode the facade enters Command mode on demand and either leaves it idle, relying on
  the module's own timeout, or exits explicitly when the application asks. In API mode the `+++`
  sequence is never used.
- API-only operations return `SL_STATUS_NOT_SUPPORTED` in Transparent mode: remote AT commands,
  arbitrary frame sending, secure session control, user data relay and Bluetooth unlock.
- The data path unifies the modern and legacy receive frames and Transparent bytes into a single
  callback, with unknown address fields marked as such in Transparent mode. Separate callbacks
  expose every unsolicited frame and modem status changes.
- `xbee_set_mode()` sends `AP` through the current transport and switches the local transport and
  escaping once the response arrives. In Command mode the change applies on exit, so the facade
  sends `AC` when leaving. Persisting with `WR` stays an explicit application call, because of the
  10 000 cycle flash limit.
- `xbee_hw_reset()` aborts pending requests, leaves Command mode, flushes the receive ring, runs
  the reset pulse and re-enters the state machine at power settle, so the mode is re-detected. In
  API mode the boot modem status frame confirms the reset. A software reset through `FR` is
  treated the same way, since the module resets 100 ms after replying.
- Sleep control covers pin sleep and cyclic sleep with pin wake. Entering refuses with
  `SL_STATUS_INVALID_STATE` while a request is pending or Command mode is active, because the
  module will not sleep then (manual 4789 to 4811); it asserts the request line and waits for the
  status line to go low within 2000 ms. Exiting de-asserts the line, waits for the status line to
  go high within 1000 ms, then waits a further 5 ms guard, which stands in for the manual's CTS
  rule at lines 4726 to 4729 since CTS is not wired on this board. While the facade believes the
  module is asleep, every send returns `SL_STATUS_NOT_READY`. Before the first sleep call the
  facade reads `SM`, `D8` and `D9` and returns `SL_STATUS_NOT_SUPPORTED` unless the module is
  configured for pin sleep. Setting those parameters stays an ordinary AT write by the
  application.

### app: `xbee_bringup`

`src/app/inc/xbee_bringup.h`, `src/app/src/xbee_bringup.c`, called from `app_init()` and
`app_process_action()`. It initialises the facade in AUTO mode with a power cycle, waits for
ready, reads `SH`, `SL`, `VR`, `HV`, `NP` and `AO`, logs them and the detected mode, then idles.
Failures are logged with the facade state and the UART statistics. This is the on-target
verification vehicle for the whole stack and the seed of the provisioning self-test.

### Build registration

In `cmake_gcc/CMakeLists.txt` only, inside the sections CLAUDE.md allows. Each `.c` file is listed
explicitly under `# Add additional sources here` in the phase that creates it:
`../src/utils/src/ring_buffer.c`, `../src/utils/src/byte_util.c`,
`../src/drivers/src/xbee_power.c`, `../src/drivers/src/xbee_reset.c`,
`../src/drivers/src/xbee_sleep.c`, `../src/drivers/src/xbee_uart.c`,
`../src/services/src/xbee_frame.c`, `../src/services/src/xbee_at_table.c`,
`../src/services/src/xbee_api.c`, `../src/services/src/xbee_cmd_mode.c`,
`../src/services/src/xbee.c`, `../src/app/src/xbee_bringup.c`. Include paths
`../src/drivers/inc`, `../src/services/inc` and `../src/app/inc` are added as each layer gets its
first file. The `target_include_directories(slc PUBLIC ...)` block is restored in phase 5, because
`app.c` is compiled inside the generated `slc` object library and will include the bring-up
header. Each `.gitkeep` is removed when its folder gets a real file.

### Host tests

A new top-level `test/` folder, built with the host compiler and never part of the target build.
`test/CMakeLists.txt` compiles one executable per test file with `-Wall -Wextra -Werror -std=c11`
and registers them with CTest. It needs `SDK_PATH` to find `sl_status.h`. `test/test_util.h`
provides a minimal assertion harness. Test files cover the ring buffer, the byte helpers, the
frame codec against the manual's worked examples including the escaped example at lines 7232 to
7243 and the checksum example at 7285 to 7314, the AT table, and the Command mode line classifier.

## Simplicity Studio changes required (PRIME RULE)

| Tool | Component / instance | Setting | Current value | Required value |
| --- | --- | --- | --- | --- |
| none | | | | |

No change is required. The receive queue depth of 6 accommodates the two receive operations, the
transmit queue holds the single transmit, `sl_gpio` and `sl_sleeptimer` are already present, and
the four control pins are already named and reserved in Pin Tool, which is visible in
`config/pin_config.h`.

Recommendations for later, outside this plan: once bring-up is stable, raise `BD` on the module
and the baud rate of the `XBEE` instance together; re-enable RTS and CTS if payload streaming ever
needs it. Both are Studio changes.

## Resource impact

Estimates, to be measured per phase with `arm-none-eabi-size`.

| Item | RAM | Flash |
| --- | --- | --- |
| `xbee_uart`, receive ring 512 B and transmit buffer 256 B | about 0.8 kB | about 0.6 kB |
| `xbee_power`, `xbee_reset`, `xbee_sleep` | a few bytes | under 0.5 kB |
| `xbee_frame`, parser buffer 256 B plus codec | 0.3 kB | 4 to 6 kB |
| `xbee_at_table`, about 147 entries without names | none | about 2.4 kB |
| `xbee_api`, 2 requests | about 0.8 kB | about 2 kB |
| `xbee_cmd_mode`, line buffer 256 B | 0.3 kB | about 2 kB |
| `xbee` facade and cached parameters | 0.2 kB | about 2 kB |
| Total | about 2.5 kB of 256 kB | about 14 kB of 1 MB |

Peripherals: EUSART0 and two LDMA channels, already allocated by UARTDRV; SYSRTC through the
sleeptimer; GPIO PA00, PB03, PB04 and PB05. Interrupts: the LDMA interrupt behind the UARTDRV
callbacks only.

Energy: no power manager is installed, so the device stays in EM0. If one is added later, UARTDRV
holds an EM1 requirement while a receive operation is queued, so the always-queued receive
operations would pin the device to EM1. `xbee_uart_deinit()` releases them.

## Risks and open questions

- The module's boot time after power-on is not stated in the manual. The settle time defaults to
  1000 ms and the AUTO probe also tolerates the boot modem status frame arriving first.
- `AO` defaults to 2, so out of the box received data arrives as `0x80` or `0x81` and I/O samples
  as `0x82` or `0x83`. The facade normalises both families; the provisioning configuration decides
  the final value.
- A response that arrives after its request has timed out is dropped as unsolicited and logged at
  verbose level.
- Command mode is fragile by design: any stray transmitted byte during the guard time restarts it,
  so the facade never transmits while a guard timer runs. The command mode deadline is tracked
  locally from the last accepted command, with a 500 ms safety margin.
- Manual quirks to re-check during phase 2: the `*X` byte range typo, the `NO` range against its
  bit field, the `SM` range printed as 0 to 5 while the table lists 6, the unit of `IF`, and the
  range printed for `$V` to `$Y`.
- `ND`, `AS` and `ED` return multi-line text in Command mode. The parser stores the lines as they
  arrive and leaves field splitting to the application; the documented format is at manual lines
  5008 to 5021.
- The host test build depends on `SDK_PATH` pointing at the Simplicity SDK for `sl_status.h`.
- `XBEE_RESET_PULSE_MS`, 10 ms, is unverified. The minimum RESET low time and the boot time after
  a reset are in the XBee 3 hardware reference manual, which is not in `docs/manuals/`.
  Recommendation: add that manual so both values can be cited. Until then the settle time and the
  re-probe make the sequence robust to the unknown boot time.
- ON_SLEEP has no CTS companion on this board, since flow control is off, so readiness after a
  wake is inferred from the status line plus a 5 ms guard rather than the manual's CTS rule. The
  phase 5 on-target test confirms that the first command after a wake is answered.
- The sleep helpers only do anything once the module is configured for pin sleep. On a factory
  module, `SM=0`, the request line has no effect and entering sleep returns
  `SL_STATUS_NOT_SUPPORTED`.

## Verification

Each phase builds with `cd cmake_gcc && cmake --workflow --preset project` and must be free of
warnings, runs the host tests that exist at that point, and gets a history entry.

1. **Foundation**: `ring_buffer`, `byte_util`, `xbee_power`, `xbee_reset`, `xbee_sleep`,
   `xbee_uart`, the build wiring and the `test/` skeleton with ring buffer and byte helper tests.
   The history entry also records the user's Pin Tool additions for PB03, PB04 and PB05.
   Verification: target build clean; host tests pass; on target, a scope or meter confirms PA00
   high after power on, a 10 ms low pulse on PB03, PB05 following the sleep request, and PB04
   reading high with a powered, awake module. An optional loopback between PB00 and PB01 proves
   the double-buffered receive path with no overruns.
2. **Codec and table**: `xbee_frame` and `xbee_at_table`. Verification: host tests against the
   manual's worked examples; target build; recorded size delta.
3. **API transport**: `xbee_api`. Verification: target build; on target with the module at `AP=1`,
   read `VR`, `SH` and `SL` and log the responses; repeat at `AP=2` to prove escaping; provoke a
   timeout by removing power from the module.
4. **Command mode transport**: `xbee_cmd_mode`, with the line classifier covered by host tests.
   Verification: on target with the module at `AP=0`, enter Command mode, read `VR`, `SH` and
   `SL`, then exit; confirm the timeout handling by waiting 11 s between commands.
5. **Facade and bring-up**: `xbee`, `xbee_bringup`, the `app.c` hooks and the `slc` include block.
   Verification: on target, AUTO mode detects each of `AP=0`, 1 and 2; `xbee_set_mode()` round
   trips between all three; the bring-up log shows `SH`, `SL`, `VR` and `HV`; the UART statistics
   report no overruns; a hardware reset produces the boot modem status frame and a successful
   re-probe; with `SM=1` set but not written to flash, entering sleep drives the status line low
   and exiting drives it high, after which the next command is answered.

Static review points for every phase: every `sl_status_t` and `Ecode_t` return value is checked;
no blocking waits; state shared with interrupt context is `volatile` and touched inside an atomic
section when more than one word is involved; every copy is bounded by an explicit length check;
the build is clean under `-Wall -Wextra`; `git status` shows no Studio-owned file.
