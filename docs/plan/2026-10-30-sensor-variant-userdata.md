# Plan: sensor hardware variant in USERDATA (contract Part 2)

Status: In progress (all phases implemented; host tests pass, firmware compiles; full link and on-target verification pending)
Date: 2026-10-30
Related history: `docs/history/2026-10-30-sensor-variant.md` (one entry for all phases)
Builds on: `docs/plan/2026-10-29-sn-userdata-storage.md` (USERDATA driver, `mfg_store`, erase command)

## Context

WTX-FW is one binary for every assembly variant. It reads two bytes in USERDATA to know which
sensors a unit has: RTD present (`0x0FE00110`) and pressure sensor type (`0x0FE00111`). A byte
left at `0xFF` means "not populated". So a unit that leaves provisioning without these bytes reads
no sensor at all in the field. This project must let the operator set both bytes, from the console
and from the per-unit hex build, next to the device SN it already writes.

### Decisions by the project owner (2026-10-30)

| Topic | Decision |
| --- | --- |
| Where it is set | Console (CLI build) **and** hex build (`tools/provision.py` / PROVISION image) |
| Console shape | One command, both axes: `device sensor-variant rtd <RTD> pressure <TYPE>` |
| Hex build inputs | Required, like `--family`/`--sequence`; PROVISION does not build without them |
| Hex-build module | Extend `sn_burner` (keep its name); new defines `SN_BURNER_RTD`, `SN_BURNER_PRESSURE` |

### Contract: what is kept and what is excluded

Kept (provisioning-relevant):
- Addresses: RTD byte at offset `0x110`, pressure byte at offset `0x111` of USERDATA. Never
  write the `0x108`-`0x10F` gap.
- Two independent axes. Pressure is one type selector (no bitmask), so at most one type.
- Values this firmware may write: RTD `0x00`/`0x01`; pressure `0x00` (none), `0x01` (1-5VDC).
  `0x02` (Modbus) and `0x03` (mV bridge) are reserved, so they are never written. No other value
  is ever written.
- `0xFF` (erased) means "not populated". `show` reports it that way.
- Values are never reassigned. New types are appended later (a table row here).

Excluded (WTX-FW runtime only):
- `lcd_show_mode_t` and how Display > Show Mode is limited to the variant.
- How WTX-FW reads the sensors: the MAX11410, the PT100 3-wire circuit, the `AIN6` divider tap,
  the Wika boost-enable GPIO.
- WTX-FW reporting "no data", and its fail-safe behaviour at run time.
- "Written externally via Commander, never by this firmware". That describes WTX-FW. Here the
  provisioning firmware writes it, as decided for the SN on 2026-10-29.

### Verified facts the design relies on

- Both bytes are in one 32-bit word (`0x110`-`0x113`) and one 64-bit double-word
  (`0x110`-`0x117`). Each double-word may be written at most twice between erases, and writing
  a 1 does not change a bit (`docs/manuals/efm32pg28_reference_manual.md`, 5.3.12). This
  firmware writes that word once per erase: the image is `{rtd, pressure, 0xFF, 0xFF}`.
- `0x110` is word-aligned, as `userdata_flash_write()` requires
  (`src/drivers/inc/userdata_flash.h`).
- No SDK token overlaps `0x110`-`0x117`.
  - Source: SDK 2025.6.2 `platform/service/token_manager/inc/sl_token_manager_manufacturing.h`.
  - `TOKEN_MFG_KIT_SIGNATURE` covers `0x104`-`0x107`.
  - **The contract text is wrong in one place.** It calls KIT_SIGNATURE the last token in the
    page, but `TOKEN_MFG_EMBER_EUI_64` is at `0x1F0` (8 bytes). The chosen addresses are still
    safe. The contract is not edited here.
- The console parser captures only `CLI_NODE_ARG` words (`src/services/inc/cli_parser.h`,
  `cli_parse_result_t.args`).
  - Keyword values would need one command id for every RTD × pressure pair, and the number
    grows with each new pressure type.
  - So the two values are ARG words, mapped by a table.

## Design

### New utils module `sensor_variant` (hardware-independent, like `device_sn`)

- `src/utils/inc/sensor_variant.h`, `src/utils/src/sensor_variant.c`
- Byte values as `#define`s, using the contract's values:
  - `SENSOR_VARIANT_RTD_ABSENT 0x00U`, `_RTD_PRESENT 0x01U`
  - `SENSOR_VARIANT_PRESSURE_NONE 0x00U`, `_1_5VDC 0x01U`, `_MODBUS 0x02U` (reserved),
    `_MV_BRIDGE 0x03U` (reserved)
  - `SENSOR_VARIANT_UNSET 0xFFU`
- `typedef struct { uint8_t rtd; uint8_t pressure; } sensor_variant_t;`
- A static table for each axis: `{ value, word, name, writable }`.
  - Pressure rows: `none`, `1-5vdc` (writable), `modbus`, `mv-bridge` (not writable).
  - When WTX-FW supports a reserved type later, enabling it is a one-field change.
- `sensor_variant_parse_rtd(word, &out)` and `sensor_variant_parse_pressure(word, &out)`:
  - Case-insensitive.
  - Accept only writable words. Anything else gives `SL_STATUS_INVALID_PARAMETER`.
- `sensor_variant_validate(const sensor_variant_t *)`: `SL_STATUS_OK` only when both bytes are
  writable values.
- `sensor_variant_rtd_name(raw)` and `sensor_variant_pressure_name(raw)` describe any byte:
  - a table name, such as "present" or "1-5VDC";
  - "Modbus, reserved" for a reserved type;
  - "not set" for `0xFF`;
  - "invalid" for anything else.

### `mfg_store` additions (`src/services/{inc,src}/mfg_store.*`)

- `MFG_STORE_VARIANT_OFFSET 0x110U` and `MFG_STORE_VARIANT_SIZE 2U`. A comment says this is
  WTX-FW's own address, not an SDK token, and that `0x108`-`0x10F` must stay unwritten.
- `_Static_assert`s:
  - the offset is word-aligned;
  - the write stays inside one word;
  - the variant bytes do not overlap the SN slot.
- `mfg_store_read_sensor_variant(sensor_variant_t *out)`: the raw bytes, `SL_STATUS_OK`.
- `mfg_store_sensor_variant_is_blank(bool *blank)`: both bytes are `0xFF`.
- `mfg_store_write_sensor_variant(const sensor_variant_t *v)`. Steps, mirroring
  `mfg_store_write_device_sn()`:
  1. `sensor_variant_validate()`, else `INVALID_PARAMETER`, with nothing written.
  2. Blank check, else `ALREADY_EXISTS`.
  3. Write the 4-byte image `{rtd, pressure, 0xFF, 0xFF}` with `userdata_flash_write()`.
  4. Read back both bytes and `memcmp`, else `FAIL`.
- The module still does not log, so it builds for the host tests.
- `mfg_store_is_blank()` and `mfg_store_erase()` are unchanged: they already cover the whole
  page.

### Console (`src/app/src/cli.c`, `cli_show.c`, `cli_config.h`)

- New tree in `conf_device_children` (M_CONF), placed alphabetically before `serial-number`:
  - `sensor-variant`, then `rtd`, then `<RTD>` ("present or absent"), then `pressure`, then
    `<TYPE>` ("none or 1-5vdc"), giving `CMD_DEVICE_VARIANT_SET`.
  - That is 6 tokens and 2 arguments, inside `CLI_PARSER_MAX_TOKENS`/`_MAX_ARGS`.
  - The hand-kept child counts change: root `device` goes to 2.
- New `show device sensor-variant` (M_ALL), giving `CMD_SHOW_DEVICE_VARIANT`, then
  `cli_show_sensor_variant()`. `show device` gets 2 children. Output:
  ```
  RTD              present (0x01)
  Pressure sensor  not set (0xFF), WTX-FW treats it as none
  ```
- `cmd_device_variant_set(rtd_word, pressure_word)`. It follows the pattern of
  `cmd_device_sn_set()` (`cli.c:1235`):
  - It parses the words. A bad word gets `% "x" is not ...: present or absent` (or `none or
    1-5vdc` for the pressure word).
  - It prints `Sensor variant  RTD present (0x01), pressure 1-5VDC (0x01)`.
  - It then reads the stored bytes:
    - **Blank:** asks `Write it to USERDATA? [N/y] ` and enters `CLI_STATE_VARIANT_CONFIRM`.
    - **Same value already stored:** `It is already stored. Nothing written.`
    - **A different value stored, or only one byte written:**
      `% Sensor variant ... is already stored. Nothing written.`, then `print_erase_hint()`.
- `handle_variant_confirm()`:
  - "y" or "yes" calls `mfg_store_write_sensor_variant()`. It then prints
    `[OK] sensor variant written: RTD present, pressure 1-5VDC`, or the error status.
  - Anything else prints `% Not written`.
- The confirm state follows `CLI_STATE_SN_CONFIRM` everywhere: the `show_answer_help()` branch,
  the line handler, Ctrl-C, and the timeout.
  - The timeout is a new `CLI_VARIANT_CONFIRM_TIMEOUT_MS 30000U` in `src/app/inc/cli_config.h`.
- `erase device userdata`:
  - The help text becomes "The whole USERDATA page, serial number and sensor variant included".
  - The question also names the variant when one is set.
  - The `show device` help becomes "Identity and sensor variant stored on this board".
- **A unit provisioned before this change** has its SN and two blank variant bytes. The variant
  word is separate from the SN slot, so `device sensor-variant ...` can write it without an
  erase and without touching the SN.

### Hex build (`sn_burner`, `app.c`, CMake, `provision.py`)

- **Build inputs:** `SN_BURNER_RTD` and `SN_BURNER_PRESSURE` are numeric byte values, for
  example `-DSN_BURNER_RTD=1 -DSN_BURNER_PRESSURE=1`.
  - `sn_burner.c` has `_Static_assert`s that each value is a writable one, so a reserved type
    cannot be built in.
  - `app.c` adds two `#error`s for PROVISION without them, beside the FF/SSSSS ones.
- **CMake** (`cmake_gcc/CMakeLists.txt`): both defines go in the `# Add additional macros here`
  block and in the `target_compile_definitions(slc PUBLIC ...)` block, in the same
  `$<$<NOT:$<STREQUAL:...>>:...>` form as `SN_BURNER_*` (lines 95-97, 115-117).
- **`burn()` order:** erase once, write the variant, then write the SN last. A unit that has an
  SN then always has its variant too, and WTX-FW sees a failed run as "not provisioned".
  1. Format the SN, which is unchanged. Then log `sensor variant RTD present (0x01), pressure
     1-5VDC (0x01)`.
  2. `report_before_erase()` also logs the variant the page held.
  3. `mfg_store_erase()`. A failure gives `serial number not written: erasing USERDATA failed`,
     as now.
  4. `mfg_store_write_sensor_variant()`:
     - On failure: `serial number not written: the sensor variant could not be written, status
       0x....`, and the run stops.
     - On success: `sensor variant written: rtd=0x01 pressure=0x01`.
  5. `mfg_store_write_device_sn()`, then the terminal line as now: `serial number written: X` or
     `serial number not written: ...`.
- **`tools/provision.py`:**
  - New required arguments `--rtd {present,absent}` and `--pressure {none,1-5vdc}`. argparse
    `choices` exclude the reserved types.
  - Words map to bytes in a dict that copies the C table.
  - The two `-D` values are added to the configure call and to `check_defines()`.
  - The variant is printed in the summary.
  - `watch_log()` matches `sensor variant written: rtd=0x(..) pressure=0x(..)`:
    - different values from the ones expected give FAIL, exit 5;
    - `serial number written` with no variant line before it also gives FAIL, exit 5.
  - The docstring, its exit-code list and the argparse description are updated. The image name
    stays `xbee_provision_<sn>.hex`.
- `sn_burner.h` doc: the new build inputs, the write order, and the extra log line.

### Host tests (`test/`)

- New `test/test_sensor_variant.c`:
  - parsing, including case and unknown or reserved words;
  - `validate`;
  - a name for every class of byte: valid, reserved, `0xFF`, invalid.
- `test/test_mfg_store.c` additions:
  - **Layout:** `0x110`, word-aligned, clear of the SN slot.
  - **Read:** blank reads `0xFF`/`0xFF`.
  - **Write:**
    - write and read back;
    - `0x112`/`0x113` stay `0xFF`;
    - `0x108`-`0x10F` stay untouched;
    - no overwrite (`ALREADY_EXISTS`);
    - reserved and invalid values are refused with write count 0;
    - write failures are passed back.
  - **SN and variant together:**
    - both can be stored, and each reads back unchanged;
    - after an erase, both can be written again.
- `test/CMakeLists.txt`:
  - `add_module_test(test_sensor_variant ...)`;
  - `test_mfg_store` also links `sensor_variant.c`.

## Phases (each: firmware build, host tests, history entry)

0. Copy this plan to `docs/plan/2026-10-30-sensor-variant-userdata.md`, Status Approved.
1. `sensor_variant` util, the `mfg_store` variant API, the host tests, and
   `../src/utils/src/sensor_variant.c` under `# Add additional sources here`.
2. Console: `device sensor-variant`, `show device sensor-variant`, and the text updates to
   `erase device userdata`.
3. Hex build: `sn_burner`, the `app.c` `#error`s, the CMake defines, and `provision.py`.
4. Docs:
   - `readme.md`: a sensor-variant subsection, the console example, and the `provision.py`
     usage.
   - `CLAUDE.md` Project facts: the variant bytes and their addresses.
   - Plan status set to Done.

## Simplicity Studio changes (PRIME RULE)

None required. `em_msc` is already in the build. The 2026-10-29 recommendation, to install
`emlib_msc` explicitly, still stands, but it is not required.

## Resource impact

- **Flash:** about 1.5-2 kB, for the tables, the console command, show and the burner step.
- **RAM:**
  - a few bytes of static data: the pending variant and the confirm deadline;
  - a 4-byte stack image.
- **Peripherals:** none added. The MSC clock is on only during the write.
- **Energy modes:** no change.
- **CPU stall:** one more word write while the super loop is blocked (the CPU stalls on flash),
  as for the SN.

## Risks and open items

1. **WTX-FW coordination.** WTX-FW must read `0x110`/`0x111` as the contract says. This project
   cannot check that.
2. **Double-word write limit.** This firmware writes `0x110`-`0x117` once per erase. If those
   bytes were also written by hand (for example with Commander), the limit of two writes could be
   exceeded. The read-back catches a wrong result.
3. **UDLOCK** is set on a unit: every write fails with "locked", as for the SN. It is reported,
   not worked around.
4. **Contract error:** it names KIT_SIGNATURE as the last token, but EUI-64 is at `0x1F0`. This
   is not a problem for this design.
5. **CMake slc define block.** `CLAUDE.md`'s allowed list names only the `slc` *include* block.
   The `slc` *compile-definitions* block already holds `SN_BURNER_*`, and this plan follows that
   precedent.
   - Recommendation, not part of this plan unless you say so: add that block to the allowed list
     in `CLAUDE.md`.

## Verification

1. Firmware build: `cd cmake_gcc && cmake --workflow --preset project`, with no warnings.
2. Host tests: `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir
   test/build --output-on-failure`, with `test_sensor_variant` and the extended
   `test_mfg_store`.
3. Hex build:
   - `python tools/provision.py --family 18 --sequence 00123 --rtd present --pressure 1-5vdc`
     builds, `check_defines()` passes, and I answer N at the flash prompt.
   - Leaving out `--rtd` is refused by argparse.
   - Configuring PROVISION without `-DSN_BURNER_RTD` stops at the `#error`.
4. On target, only with the owner's go-ahead:
   - **CLI:** `device sensor-variant rtd present pressure 1-5vdc`, then
     `show device sensor-variant`.
   - **J-Link MCP `read_memory`**, 8 bytes at `0x0FE00110`: expect
     `01 01 FF FF FF FF FF FF`. The bytes at `0x0FE00108`-`0x0FE0010F` should still read `0xFF`,
     and the SN slot should be unchanged.
   - **Erase:** `erase device userdata` names the variant, and the page reads all `0xFF`
     afterwards.
   - **Hex flash-and-watch:** `provision.py` reports PASS, with the variant line before the SN
     line.
5. Each history entry says which of these were run: build only, host tests, or on target.

## Files touched

- **New:**
  - `src/utils/{inc,src}/sensor_variant.*`
  - `test/test_sensor_variant.c`
  - `docs/plan/2026-10-30-sensor-variant-userdata.md`
  - the history entries
- **Changed:**
  - `src/services/{inc,src}/mfg_store.*`
  - `src/app/src/{cli.c,cli_show.c,cli_show.h,sn_burner.c}`
  - `src/app/inc/{cli_config.h,sn_burner.h}`
  - `app.c`, `cmake_gcc/CMakeLists.txt`
  - `test/{CMakeLists.txt,test_mfg_store.c}`
  - `tools/provision.py`, `readme.md`, `CLAUDE.md`
- **Not touched:** anything Studio-owned (`config/`, `autogen/`, `.slcp`, `.pintool`, the
  generated CMake files).
