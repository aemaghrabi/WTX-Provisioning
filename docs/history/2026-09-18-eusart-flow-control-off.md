# XBee EUSART0: RTS/CTS hardware flow control disabled

Date: 2026-09-18
Related plan: none

## Summary

Hardware flow control on the XBee UART (EUSART0, UARTDRV instance `XBEE`) was turned off in
Simplicity Studio: `SL_UARTDRV_EUSART_XBEE_FLOW_CONTROL_TYPE` changed from `uartdrvFlowControlHwUart`
to `uartdrvFlowControlNone`, and the CTS (PB02) and RTS (PB06) routes were removed in Pin Tool.
The documentation was updated to match, and PA00 (`XBEE_EN_GPIO`) is now documented as the GPIO
that drives the XBee power-enable load switch.

## Motivation

Temporary bring-up simplification: the XBee link is brought up without the RTS/CTS handshake so
that only RX/TX have to be correct. Flow control is expected to be restored later.

The documentation update also covers the pin and component facts that had gone stale after the
VCOM logging work of 2026-09-17 (`docs/history/2026-09-17-vcom-printf.md`,
`docs/history/2026-09-17-multi-level-log.md`): the log UART (EUSART2, PD08/PD07) and the
`iostream_eusart` / `iostream_retarget_stdio` components were not listed.

## Changes

| File | Change |
| --- | --- |
| `readme.md` | Hardware table: XBee UART now "no flow control"; added the log UART row. Pin table: removed CTS/RTS rows, added PD08/PD07 log pins, renamed the PA00 row to "XBee power enable" and described its polarity. Added a note on why flow control is off. Component list extended with `iostream_eusart` (`VCOM`), `iostream_retarget_stdio` and `app_log`. |
| `CLAUDE.md` | Project facts: component list extended. "XBee interface" table: CTS/RTS rows removed, UART line now "no flow control", added the `XBEE_EN_GPIO` load-switch description and a note on the disabled flow control and the `autogen` CTS/RTS fallback. New "Log interface" section for EUSART2. |
| `docs/history/2026-09-18-eusart-flow-control-off.md` | This entry. |

No application source changed.

## Simplicity Studio changes (made by the user)

| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |
| Component editor | `uartdrv_eusart` / `XBEE` | Flow control type | `uartdrvFlowControlHwUart` | `uartdrvFlowControlNone` |
| Pin Tool | EUSART0 | CTS route (PB02) | Enabled | Removed |
| Pin Tool | EUSART0 | RTS route (PB06) | Enabled | Removed |
| Pin Tool | PB02, PB06 | Pin name | (route-assigned) | cleared |

Generated effect: the `SL_UARTDRV_EUSART_XBEE_CTS_*` / `_RTS_*` and `EUSART0_CTS_*` / `EUSART0_RTS_*`
macros are gone from `config/sl_uartdrv_eusart_XBEE_config.h` and `config/pin_config.h`.

## Verification

Static review only; not built or run on target as part of this change.

Reviewed against the SDK source `platform/emdrv/uartdrv/src/uartdrv.c` (Simplicity SDK 2025.6.2):

- `UARTDRV_InitEusart()` configures the CTS/RTS pins only when `fcType` is `uartdrvFlowControlHw` or
  `uartdrvFlowControlHwUart` (lines 1234-1245), and writes the `GPIO` CTS/RTS route registers only
  for `uartdrvFlowControlHwUart` (lines 1519-1552).
- `autogen/sl_uartdrv_init.c:13-22` falls back to `SL_GPIO_PORT_A` pin 0 when the CTS/RTS macros are
  undefined, which is the same pin as `XBEE_EN_GPIO`. Because `fcType` is now
  `uartdrvFlowControlNone`, neither the pin mode nor the route register is written, so this fallback
  cannot disturb PA00.

## Known limitations and follow-ups

- Without RTS/CTS the XBee cannot throttle the MCU and the MCU cannot throttle the XBee. The
  UARTDRV `XBEE` RX/TX buffer size of 6 makes RX overrun likely at 9600 baud if the application does
  not keep a receive operation queued; raising that buffer size (component editor,
  `uartdrv_eusart` / `XBEE`) is recommended before any real AT command traffic.
- Restoring flow control later requires re-enabling the EUSART0 CTS/RTS routes in Pin Tool and
  setting the flow control type back to `uartdrvFlowControlHwUart`. The XBee side must match
  (`D6`/`D7`, see `docs/manuals/xbee_90002273_ref_manual.md`, "RTS flow control" and "Clear-to-send
  (CTS) flow control").
- `xbee_provision.pintool` also gained an `EUSART1` entry (included, RX/TX location 16/17, no enable
  properties) in the same working tree. The user confirmed it is an unintended Pin Tool side effect;
  it is not documented as part of the interface and can be removed in Pin Tool.
- The PA00 polarity recorded here (high = XBee powered) comes from the user, not from a schematic in
  the repo.
