# xbee_provision

Provisioning firmware for brand-new boards. It is flashed onto a fresh board first, prepares the
hardware (XBee radio configuration, device data, self-test), reports the result, and is then
replaced by the main product firmware.

> Status: early development. The project currently contains the Simplicity Studio skeleton only;
> no provisioning logic is implemented yet. Items marked **TBD** are not yet defined.

## Scope

The provisioning firmware is responsible for:

1. **Hardware self-test** -- confirm the board is fit to provision (for example: the XBee module
   responds on its UART and reports an acceptable firmware version). Pass/fail criteria: **TBD**.
2. **Read and record IDs** -- read device identifiers (for example the XBee 64-bit serial number,
   `SH`/`SL`, and the EFM32 unique ID) and report them to production tooling. Exact list: **TBD**.
3. **XBee radio configuration** -- put the XBee 3 802.15.4 module into the configuration the main
   firmware expects, using AT commands, and persist it on the module (`WR`). Implemented, not yet
   run on hardware; the parameter set is `src/app/inc/xbee_provision_config.h`, which covers every
   parameter the module stores and ships holding the factory defaults. See
   [XBee radio configuration](#xbee-radio-configuration).
4. **Write MCU NVM data** -- store provisioning data on the EFM32 that the main firmware reads at
   runtime. Data content and storage layout: **TBD**.
5. **Report the result** -- over a UART log to a PC and via the debugger (RTT). Details: **TBD**.

Out of scope: the main product firmware itself, and loading it. After provisioning, the main
firmware is flashed over SWD by production (see [Production use](#production-use)).

## Hardware

| Item | Detail |
| --- | --- |
| MCU | Silicon Labs EFM32PG28B210F1024IM68 (custom board, no Silicon Labs kit) |
| Radio | Digi XBee 3 802.15.4 RF module |
| XBee UART | EUSART0, 9600 baud, 8N1, no flow control |
| Log UART (VCOM) | EUSART2, 115200 baud, 8N1, no flow control |

| Signal | MCU pin |
| --- | --- |
| XBee UART RX (EUSART0 RX) | PB00 |
| XBee UART TX (EUSART0 TX) | PB01 |
| XBee power enable (`XBEE_EN_GPIO`) | PA00 |
| XBee reset (`XBEE_nRESET`) | PB03 |
| XBee sleep status (`XBEE_nSLEEP_Status`) | PB04 |
| XBee sleep request (`XBEE_SLEEPRQ`) | PB05 |
| Log UART TX (EUSART2 TX) | PD08 |
| Log UART RX (EUSART2 RX) | PD07 |

`XBEE_EN_GPIO` (PA00) is a push-pull output that drives the load switch supplying the XBee module:
high powers the module, low removes its supply.

`XBEE_nRESET` (PB03) is the module's active-low reset input, driven open drain so the MCU only
pulls it low and the module pull-up releases it. `XBEE_SLEEPRQ` (PB05) drives the module's
DTR/SLEEP_RQ input and `XBEE_nSLEEP_Status` (PB04) reads its ON_SLEEP output, high meaning awake.
Both sleep lines only carry meaning when the module is configured for pin sleep (`SM` 1 or 5,
`D8`=1, `D9`=1).

RTS/CTS hardware flow control on the XBee link is currently disabled to simplify bring-up; PB02
(CTS) and PB06 (RTS) are no longer routed to EUSART0. Restoring it is a Pin Tool and
`uartdrv_eusart` (`XBEE`) configuration change, not an application change.

Reporting over RTT is not configured yet: **TBD**.

## Software

- Simplicity SDK 2025.6.2, Simplicity Studio 6, VS Code generator, CMake + Ninja, GCC 12.2.1
- Bare-metal super loop: `app_init()` once, `app_process_action()` every loop iteration
  (`app.c`)
- Installed software components: `clock_manager`, `device_init`, `sl_main`,
  `uartdrv_eusart` (instance `XBEE`), `iostream_eusart` (instance `VCOM`),
  `iostream_retarget_stdio`
- Logging: `app_log` (`src/utils/`), levelled `printf` output over the VCOM UART
- XBee stack (implemented, not yet run on hardware; see
  `docs/plan/2026-09-18-xbee3-driver-stack.md`):
  - `src/utils/`: `ring_buffer` byte FIFO, `byte_util` big-endian and hexadecimal helpers
  - `src/drivers/`: `xbee_power` supply switch, `xbee_reset` reset line, `xbee_sleep` sleep
    request and status lines, `xbee_uart` byte transport over the UARTDRV `XBEE` instance
  - `src/services/`: `xbee_frame` codec for all 26 documented API frames with a streaming
    parser, `xbee_at_table` describing all 147 AT commands, `xbee_api` and `xbee_cmd_mode`
    transports, and `xbee`, the facade that hides which mode the module is in
  - `src/app/`: `xbee_bringup`, which powers the module, detects its mode and logs what it found,
    and `xbee_provision`, which configures the module from a header
    (see `docs/plan/2026-09-19-xbee-provision-app.md`)

The same application calls work whether the module is in Transparent mode, API mode 1 or API
mode 2. The facade detects which, on its own, at start-up.

### XBee radio configuration

`src/app/inc/xbee_provision_config.h` holds the configuration the module is provisioned to: one
macro per parameter the module stores, 108 in all, each preloaded with the manual's factory
default. Edit the values there; nothing else needs to change.

At boot the provisioning application reads the module's configuration and compares it. If
everything matches it stops without writing, so a board that has already been provisioned does not
spend one of the module's 10 000 flash cycles. Otherwise it restores the module's defaults, writes
only the parameters the header sets away from the default, commits them with a single `WR`, resets
the module and reads everything back to confirm.

The whole sequence runs inside one Command mode session. Parameters set in Command mode are staged
until the session ends, so even changes that would otherwise break the serial link, such as `AP` or
`BD`, can be written safely and applied together at the reset.

Which application runs is chosen by `XBEE_APP` in `cmake_gcc/CMakeLists.txt`:
`XBEE_APP_PROVISION` (the default) or `XBEE_APP_BRINGUP`, which only detects and reports, writing
nothing to the module.

The configured values are checked against the AT command table by the host test
`test_xbee_provision_table`, which also prints exactly which parameters a run would write. A value
outside the documented range, or a string too long for its command, fails the build rather than the
board. The UART settings are additionally checked against the generated `XBEE` instance
configuration at compile time.

Planned features will need additional components (for example NVM storage and RTT). These are
added only through Simplicity Studio and will be specified in the relevant plan under
`docs/plan/`.

### Repository layout

| Path | Contents |
| --- | --- |
| `app.c`, `app.h` | Application code (provisioning logic) |
| `main.c` | Entry point and super loop |
| `src/app/` | New application modules: provisioning sequencing (`inc/` public headers, `src/` sources) |
| `src/services/` | New application modules: protocol and feature logic, e.g. XBee AT, NVM storage, reporting |
| `src/drivers/` | New application modules: board-level wrappers over SDK driver APIs |
| `src/utils/` | New application modules: hardware-independent helpers |
| `test/` | Host-side unit tests for the hardware-independent modules |
| `config/`, `autogen/`, `*.slcp`, `*.slps`, `*.pintool` | Simplicity Studio generated -- **do not edit by hand** |
| `cmake_gcc/` | CMake build files |
| `docs/manuals/` | Reference manuals (EFM32PG28, XBee 3 802.15.4) |
| `docs/plan/` | Design and implementation plans |
| `docs/history/` | Change history |
| `CLAUDE.md` | Project rules for development |

## Development rules

Files generated by Simplicity Studio must never be edited by hand. All changes to HAL
configuration, pin map, and software components are made in Simplicity Studio (Project
Configurator, component editors, Pin Tool). See `CLAUDE.md` for the full set of project rules.

## Building

1. Open the project in Simplicity Studio 6 and let it generate (required after a fresh clone,
   because machine-specific CMake files are not in git).
2. Build from VS Code with the Silicon Labs extension, or from a terminal:

   ```sh
   cd cmake_gcc
   cmake --workflow --preset project
   ```

3. Output images: `cmake_gcc/build/base/xbee_provision.{out,hex,bin,s37}`

### Host unit tests

Modules that depend on nothing beyond `sl_status.h` and the C standard library are covered by
tests that build with the host compiler, separately from the firmware image:

```sh
cmake -S test -B test/build
cmake --build test/build
ctest --test-dir test/build --output-on-failure
```

The test build finds `sl_status.h` through `SDK_PATH`, which it reads from the generated
`cmake_gcc/xbee_provision.cmake`. Pass `-DSDK_PATH=/path/to/simplicity_sdk` to override it.

## Production use

Flow for each new board (procedure details **TBD**):

1. Connect the board to a debug probe (SWD) and, for the log, to a PC UART.
2. Flash the provisioning image `xbee_provision.s37` over SWD (for example with Simplicity
   Commander).
3. Let the board run provisioning; observe the result on the UART log or via RTT.
4. On pass: flash the main product firmware over SWD. On fail: set the board aside for
   investigation and record the reported error.
