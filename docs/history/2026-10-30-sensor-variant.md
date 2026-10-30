# Sensor hardware variant in USERDATA

Date: 2026-10-30
Related plan: docs/plan/2026-10-30-sensor-variant-userdata.md

## Summary
The provisioning firmware can now write the sensor hardware variant, two bytes WTX-FW reads from
USERDATA (RTD at `0x0FE00110`, pressure sensor type at `0x0FE00111`), from the console and from the
per-unit hex build. All four phases of the plan are in this entry.

## Motivation
WTX-FW is one binary for every assembly variant and reads these bytes to know which sensors a unit
has. A unit that leaves provisioning without them is read as having no sensors. The contract part
that covers only WTX-FW run time (LCD show mode, sensor reads, the boost-enable GPIO, "no data"
reporting) is not implemented here.

## Changes
| File | Change |
| --- | --- |
| `src/utils/inc/sensor_variant.h`, `src/utils/src/sensor_variant.c` | New. Byte values, the typed words, which values may be written, and a name for any stored byte. The reserved pressure types are named but never accepted for writing. |
| `src/services/inc/mfg_store.h`, `src/services/src/mfg_store.c` | Added `mfg_store_read_sensor_variant()`, `mfg_store_sensor_variant_is_blank()` and `mfg_store_write_sensor_variant()`. One 4-byte word `{rtd, pressure, 0xFF, 0xFF}` at offset `0x110`, only over erased bytes, read back. Static asserts keep it clear of the serial number slot. |
| `src/app/src/cli.c`, `cli_show.c`, `cli_show.h`, `src/app/inc/cli_config.h` | New `device sensor-variant rtd <present\|absent> pressure <none\|1-5vdc>` with a `[N/y]` question (`CLI_STATE_VARIANT_CONFIRM`, `CLI_VARIANT_CONFIRM_TIMEOUT_MS`), `show device sensor-variant`, and `erase device userdata` now names the variant it removes. |
| `src/app/src/sn_burner.c`, `src/app/inc/sn_burner.h` | Build inputs `SN_BURNER_RTD` and `SN_BURNER_PRESSURE`, checked by static asserts. Order is now: validate, erase, write variant, write serial number. New log line `sensor variant written: rtd=0x.. pressure=0x..`. |
| `app.c` | Two `#error`s for the PROVISION build without `SN_BURNER_RTD` or `SN_BURNER_PRESSURE`; comment corrected from NVM3 to USERDATA. |
| `cmake_gcc/CMakeLists.txt` | `sensor_variant.c` under `# Add additional sources here`; the two defines in both `target_compile_definitions` blocks, as `SN_BURNER_*` already are. |
| `tools/provision.py` | Required `--rtd {absent,present}` and `--pressure {1-5vdc,none}`, passed as defines and checked in the compile database. The log watcher fails (exit 5) if the variant line is missing or differs. |
| `test/test_sensor_variant.c` (new), `test/test_mfg_store.c`, `test/CMakeLists.txt` | Host tests for the module and for the variant in `mfg_store`. |
| `readme.md`, `CLAUDE.md`, plan | Sensor variant section, usage, project fact, plan status. |

## Simplicity Studio changes (made by the user)
| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |

None.

## Verification
- **Host tests:** `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir
  test/build`: 16 of 16 pass, including the new `test_sensor_variant` and the extended
  `test_mfg_store`.
- **Firmware compile:** the console build and a PROVISION build (`-DSN_BURNER_RTD=1
  -DSN_BURNER_PRESSURE=1`) compile every changed file with no warnings. The link was not reached:
  see the first limitation.
- **Build guards:** configuring PROVISION without the variant stops at the `#error` in `app.c`;
  `-DSN_BURNER_PRESSURE=2` stops at the static assert in `sn_burner.c`.
- **`provision.py`:** a missing `--rtd`/`--pressure` and `--pressure modbus` are refused by
  argparse. The log watcher was run against a fake serial port: pass, wrong variant, missing
  variant line, not-written and timeout give exit codes 0, 5, 5, 4 and 6.
- **Not done:** no hex image was built or flashed, and nothing was run on target.

## Known limitations and follow-ups
- **The generated build is stale, so the firmware does not link on this machine.**
  `cmake_gcc/xbee_provision.cmake` dates from 2026-09-18 and has no NVM3 entries, while the
  `.slcp` gained `nvm3_default` on 2026-10-06 and `autogen/sl_event_handler.c` includes
  `nvm3_default.h`. This predates this change. In Simplicity Studio, open the project and let it
  regenerate (Project Configurator, Save/Generate), then rebuild. Nothing here was edited for it.
- On-target checks from the plan are pending: console write and show, an 8-byte read at
  `0x0FE00110` expecting `01 01 FF FF FF FF FF FF` with `0x108` to `0x10F` still `0xFF`, erase,
  and a hex flash-and-watch.
- The contract calls `TOKEN_MFG_KIT_SIGNATURE` the last SDK token in the page, but
  `TOKEN_MFG_EMBER_EUI_64` is at `0x1F0`. The chosen addresses do not overlap it.
- WTX-FW must read `0x110` and `0x111` as the contract says; this project cannot check that.
- Recommendation, not done: add the `target_compile_definitions(slc PUBLIC ...)` block to the
  allowed list in `CLAUDE.md`, since the `SN_BURNER_*` and now the variant defines live there.
