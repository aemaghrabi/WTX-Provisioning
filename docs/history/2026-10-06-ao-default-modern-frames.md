# Provision AO = 0 by default (GitHub issue #3)

Date: 2026-10-06
Related plan: docs/plan/2026-10-06-ao-default-modern-frames.md

## Summary

The default provisioning configuration now sets `AO` (API Output Options) to `0` instead of the
factory default `2`, so a provisioned module emits the Receive Packet `0x90` and I/O Sample
Indicator `0x92` instead of the legacy `0x80`/`0x81` and `0x82`/`0x83` frames.

## Motivation

GitHub issue #3, "Change XBee default provision to use new Digi Frame types by default". The
manual recommends `AO=0` for new designs (`docs/manuals/xbee_90002273_ref_manual.md`, lines 6019
to 6022). The issue also names the Transmit Request `0x10`; `AO` does not control transmit frames,
and this firmware already builds `0x10` rather than `0x00`/`0x01`, so nothing was needed for that
part.

## Changes

| File | Change |
| --- | --- |
| `src/app/inc/xbee_provision_config.h` | `XBEE_PROV_AO` `2U` -> `0U`. Doxygen comment now explains values 0, 1 and 2, the mixed-network case for 2, and that the transmit frame type is unaffected |
| `src/app/src/xbee_bringup.c` | Comment above the `ao == 2` warning: 2 after provisioning means the module was not provisioned with the default configuration. Code unchanged |
| `docs/plan/2026-10-06-ao-default-modern-frames.md` | New plan |
| `docs/history/2026-10-06-ao-default-modern-frames.md` | This entry |

## Simplicity Studio changes (made by the user)

| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |

None.

## Verification

Static review only. This machine has no CMake, host compiler or ARM toolchain and the project has
not been generated here, so neither the firmware build nor the host tests were run. Expected by
review: `test_xbee_provision_table` accepts `0` (range 0 to 2); `test_xbee_at_table` still passes,
since it checks the factory default of `AO` in the command table, which is unchanged.

## Known limitations and follow-ups

- Build the provisioning, CLI and bridge variants and run the host tests.
- On target: confirm `AO` is written, bring-up reports `output options 0` without the legacy-frame
  warning, and peer data arrives as `0x90`.
- A node provisioned this way sends I/O samples that legacy S1/S2C devices cannot interpret
  (manual lines 3435 to 3436). A build for a mixed network must pass `-DXBEE_PROV_AO=2U`.
- Host software on the VCOM bridge that parses only `0x80`/`0x81` must handle `0x90`.
