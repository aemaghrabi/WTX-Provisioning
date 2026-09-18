# jlink-mcp registered as a project MCP server

Date: 2026-09-19
Related plan: none

## Summary

The `jlink-mcp` MCP server (Klievan/jlink-mcp, npm `jlink-mcp` 0.7.0) was registered at project
scope in a new `.mcp.json`, configured for the EFM32PG28 target over SWD. It gives an MCP-aware
client (Claude Code, Copilot Chat) tools to drive the SEGGER J-Link probe: flash, halt, resume,
step, read/write memory and registers, breakpoints, RTT streaming, a GDB server bridge, and
ARM Cortex-M fault decoding.

This is a tooling/debug-host change only. No firmware source, no Simplicity Studio configuration
and no build files were touched.

## Motivation

The user asked for `jlink-mcp` to be installed. No further reason was stated and none is inferred
here.

## Changes

| File | Change |
| --- | --- |
| `.mcp.json` | New. Registers one server, `jlink`, run as `npx -y jlink-mcp`, with `PROBE_TYPE=jlink`, `JLINK_DEVICE=EFM32PG28BxxxF1024`, `JLINK_INTERFACE=SWD`, `JLINK_SPEED=4000`. |
| `docs/history/2026-09-19-jlink-mcp-install.md` | This entry. |

`.mcp.json` is project scope and is not covered by `.gitignore`, so it is shared with anyone who
clones the repository. The choice of project scope over user scope was made by the user.

## Simplicity Studio changes (made by the user)

None. This change does not touch any Studio-owned file.

## Environment as found

| Item | Value |
| --- | --- |
| SEGGER J-Link Commander | V8.64, `/Applications/SEGGER/JLink_V864` |
| Node.js | v22.18.0 (server requires 18+) |
| npm | 11.6.1 |
| `jlink-mcp` | 0.7.0, bin `jlink-mcp` -> `out/mcp/standalone.js` |

## Verification

The server was started out of band over stdio with the same environment as `.mcp.json` and driven
with hand-written JSON-RPC. No firmware was flashed and nothing was run on target.

- `initialize` returned `serverInfo` `jlink-mcp` 0.7.0 and advertised tools, resources and prompts.
- `tools/list` returned the tool set (`list_devices`, `check_setup`, `search_devices`, flash/halt/
  step, RTT, GDB, and others).
- `search_devices` with query `EFM32PG28` returned exactly two J-Link device names:
  `EFM32PG28BxxxF512` and `EFM32PG28BxxxF1024`.
- `check_setup` reported "Ready to debug", with the probe software found, a probe connected, and
  the target device accepted as `EFM32PG28BxxxF1024`.

### Device name correction

The part on this project is EFM32PG28B210F1024IM68 (`CLAUDE.md`, "Project facts"). `.mcp.json` was
first written with that full orderable part number as `JLINK_DEVICE`; `search_devices` showed
J-Link does not use it. J-Link identifies the family by the wildcard form, so `JLINK_DEVICE` was
corrected to `EFM32PG28BxxxF1024`, which matches the 1024 KiB flash variant.

Passing an unknown name to `JLinkExe -device` is not diagnosed at launch, only at connect time, so
the name cannot be validated by starting J-Link Commander without a probe attached.

## Known limitations and follow-ups

- `check_setup` reports no SVD is loaded, so peripheral reads come back as raw hex rather than
  named register fields. No EFM32PG28 `.svd` exists anywhere under `~/.silabs` on this machine, so
  `SVD_PATH` was left unset. Obtaining the EFM32PG28 SVD (Silicon Labs CMSIS device pack) and
  setting `SVD_PATH` in `.mcp.json` would make `read_peripheral` legible.
- `JLINK_RTT_ADDR` is unset, so RTT cannot be recovered after a reset or a flash. It is only
  relevant once the firmware links an RTT control block; the project currently logs over the
  `iostream_eusart` `VCOM` instance (EUSART2, PD08/PD07) instead, not RTT.
- No ELF is loaded, so GDB backtraces are unnamed until `gdb_load` is pointed at
  `cmake_gcc/build/base/xbee_provision.out`.
- The server is fetched with `npx -y jlink-mcp` at every launch, so the version is not pinned and
  can change without a repository change. Pinning (`jlink-mcp@0.7.0`) or a local devDependency
  would make it reproducible.
- These tools can halt, erase and reprogram the attached target. Anyone who clones the repository
  and opens it in an MCP-aware client inherits this configuration.
- `check_setup` found a probe connected at the time of this change; no further on-target operation
  was performed.
