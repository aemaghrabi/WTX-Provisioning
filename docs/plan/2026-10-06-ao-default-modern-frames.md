# Provision AO = 0 by default (GitHub issue #3)

Status: Done, pending build, host-test and on-target verification
Date: 2026-10-06
Related history: docs/history/2026-10-06-ao-default-modern-frames.md

Implemented as designed. Checked by static review only: this machine has no
CMake, host compiler or ARM toolchain, and the project has not been generated
here, so none of the checks under "Verification" have been run.

## Goal

Resolve GitHub issue #3, "Change XBee default provision to use new Digi Frame types by default":
an unedited `src/app/inc/xbee_provision_config.h` provisions the module with `AO` (API Output
Options) set to `0` instead of the factory default `2`, so that a provisioned module emits the
standard Receive Packet `0x90` and I/O Sample Indicator `0x92` instead of the legacy `0x80`/`0x81`
and `0x82`/`0x83` frames.

Measurable outcome: after provisioning, the bring-up log reports `output options 0` and does not
print the legacy-frame warning, and data sent from a peer arrives as a `0x90` frame.

## Background and references

- `docs/manuals/xbee_90002273_ref_manual.md`, lines 6014 to 6037 (`AO`): range 0 to 2, default 2.
  `AO=0` is recommended for new designs, emits `0x90` for received data and `0x92` for I/O
  samples, and allows all 15 I/O lines (D0 to P4) to be sampled. `AO=2` emits the legacy frames
  and restricts digital sampling to D0 to D8. `AO=1` emits the Explicit Receive Indicator `0x91`.
- Same manual, lines 3435 to 3438 (Mixed network considerations): in a network mixing XBee3 with
  legacy S1 or S2C devices, `AO` must be `2` for transmitted sample data to be compatible with
  them. Samples received from S1/S2C devices are always output in the legacy format.
- `AO` affects only frames the module **outputs** on its serial interface. The transmit frame type
  is chosen by the host; this firmware already builds the Transmit Request `0x10`
  (`src/services/src/xbee_frame.c`) and never the deprecated `0x00`/`0x01`. The "transmit `0x10`"
  part of the issue therefore needs no module setting.

## Design

- `XBEE_PROV_AO` in `src/app/inc/xbee_provision_config.h` changes from `2U` to `0U`. The `#ifndef`
  guard stays, so a build for a mixed network can still pass `-DXBEE_PROV_AO=2U`.
- The provisioning engine writes only parameters whose configured value differs from the factory
  default, so `AO` becomes one of the written parameters with no engine change. `AO` is already in
  the provision list (`src/app/src/xbee_provision_list.h`).
- The receive path needs no change: `on_api_frame()` in `src/services/src/xbee.c` already
  normalises `0x90`, `0x80` and `0x81` into the same data callback.
- `src/app/src/xbee_bringup.c`: the comment above the `ao == 2` warning is reworded. The module
  ships with `AO=2`; seeing it after provisioning means the module has not been provisioned with
  this configuration. The warning itself is kept.

## Simplicity Studio changes required (PRIME RULE)

| Tool | Component / instance | Setting | Current value | Required value |
| --- | --- | --- | --- | --- |

None. The change is application code only.

## Resource impact

No RAM, flash, peripheral, interrupt or energy-mode impact on the MCU: one constant changes. On the
module, one more parameter is written per provisioning, within the single `WR`, which is negligible
against the 10 000 erase and write cycles (manual lines 7089 to 7099).

## Risks and open questions

- Mixed networks: a node provisioned with `AO=0` sends I/O samples in the new format, which legacy
  S1/S2C receivers cannot interpret (manual line 3435). Acceptable only if the fleet is XBee3-only;
  to be confirmed by the project owner.
- Host software reading the module through the VCOM bridge build that parses only `0x80`/`0x81`
  must be updated to handle `0x90`.

## Verification

1. Firmware build of the provisioning, CLI and bridge variants with zero warnings.
2. Host tests `test_xbee_provision_table` (range check of the new value) and `test_xbee_at_table`
   (the factory default of `AO` is still 2) pass.
3. On target: flash the provisioning build and check the VCOM log shows `AO` written, then at the
   post-reset bring-up `output options 0` with no legacy-frame warning; send data from a peer and
   confirm a `0x90` frame is received.
