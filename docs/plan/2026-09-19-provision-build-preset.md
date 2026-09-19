# Standalone provisioning build preset

Status: Done, pending on-target verification
Date: 2026-09-19
Related history: docs/history/2026-09-19-provision-build-preset.md

## Goal

Produce a dedicated provisioning firmware image, with logs, from one command:

```sh
cd cmake_gcc
cmake --workflow --preset provision
# -> cmake_gcc/build-provision/base/xbee_provision.{out,hex,bin,s37}
```

The image contains the provisioning application only: `src/app/src/xbee_bringup.c`
is not compiled into it at all. The default `project` preset and its
`cmake_gcc/build/` output are left exactly as they are, byte for byte.

## Context

Two things stand in the way today.

- **The application is selected by hand.** `cmake_gcc/CMakeLists.txt` hardcodes
  `XBEE_APP=XBEE_APP_PROVISION` in two places, once on the `xbee_provision`
  target and once on the generated `slc` target (`app.c` is compiled inside
  `slc`, which does not inherit the executable's definitions). Building the
  bring-up diagnostic means editing both lines, and remembering to put them
  back.
- **Both applications are always compiled in.** The source list names
  `xbee_bringup.c`, `xbee_provision.c` and `xbee_provision_table.c`
  unconditionally. `XBEE_APP` only chooses which one `app.c` calls; the other
  one is still compiled and handed to the linker.

The second point was expected to be a production concern: a provisioning image
that ships with the bring-up application sitting unreferenced in flash would be
carrying code never meant to run on a board.

**Measured during implementation, it is not.** The build links with
`-Wl,--gc-sections` and compiles with `-ffunction-sections -fdata-sections`
(`cmake_gcc/xbee_provision.cmake:132,133,175`), so the unreferenced application
was already being discarded, and the standalone image comes out byte-identical
to the default one. What remains, and is worth doing, is that the selection
becomes a build input rather than a hand edit in two places, that the
provisioning firmware is built from provisioning sources only, and that a wrong
selection becomes a diagnostic instead of a silent fallback.

`src/app/src/xbee_dump.c` was deliberately split out as its own module for this
change (`docs/plan/2026-09-19-xbee-parameter-dump.md`, "Layering"): it is the
shared boot parameter dump and must survive the exclusion of either
application.

## Background and references

No MCU or XBee behaviour is involved in this change, so `docs/manuals/` is not
cited. The references are the build system's own contract and the project rules.

| Topic | Source | What it establishes |
| --- | --- | --- |
| `CMakePresets.json` is regenerated | `CLAUDE.md`, "Forbidden to edit"; `.gitignore:27` | The Studio-generated presets file is both off limits and untracked, so a preset added to it is lost on the next generation and never reaches another machine |
| `CMakeUserPresets.json` | CMake `cmake-presets(7)` | CMake reads it automatically from the same directory as `CMakePresets.json`, and presets in it may inherit from presets in `CMakePresets.json`. Studio does not generate it |
| Build registration is application-owned | `CLAUDE.md`, "Allowed to edit" | `cmake_gcc/CMakeLists.txt` may be changed inside the `# Add additional ...` sections |
| Log level floor | `src/utils/inc/app_log_config.h:15-17` | `APP_LOG_LEVEL_MIN` is `#ifndef`-guarded specifically so a build can override it with a compiler define instead of an edit |
| Level macros | `src/utils/inc/app_log.h:34-38, 44` | `APP_LOG_LEVEL_VERBOSE` through `_NONE` are defined *above* the `#include "app_log_config.h"`, and `app_log_config.h` is included from nowhere else in the tree |

## Design

### Where the preset lives

A new, tracked `cmake_gcc/CMakeUserPresets.json`.

`cmake_gcc/CMakePresets.json` is the obvious place and the wrong one. It is
Studio-generated, so the PRIME RULE forbids editing it, and it is git-ignored
(`.gitignore:27`, `cmake_gcc/CMakePresets.json`), so anything added to it would
be destroyed on the next generation and would never leave this machine.

`CMakeUserPresets.json` is the file CMake provides for exactly this: it is read
automatically alongside `CMakePresets.json`, its presets may inherit from
presets in `CMakePresets.json`, and Studio never writes it. `.gitignore`'s
`cmake_gcc/*.cmake` pattern does not match a `.json` file and the explicit
`cmake_gcc/CMakePresets.json` line names only that one file, so the new file is
tracked with no `.gitignore` change.

The name is CMake's convention for a *user's* presets and such a file is often
left untracked. Tracking it here is deliberate: it is the only Studio-safe place
in this project for a preset the whole team should have.

### Preset contents

Three presets, all named `provision`:

| Preset | Inherits | Overrides |
| --- | --- | --- |
| configure `provision` | configure `project` | `binaryDir` is `${sourceDir}/build-provision`; cache variables `XBEE_APP_SELECT=PROVISION` and `XBEE_LOG_LEVEL=INFO` |
| build `provision` | nothing | `configurePreset` `provision`, configuration `base`, target `xbee_provision` |
| workflow `provision` | nothing | configure `provision`, then build `provision` |

Inheriting the configure preset is what keeps this change small and stable: the
generator (`Ninja Multi-Config`), the toolchain file and
`CMAKE_CONFIGURATION_TYPES=base` all come from the generated file and are not
restated. Cache variables merge per key, so `CMAKE_CONFIGURATION_TYPES` survives
the override of the two new ones.

The separate `binaryDir` is the reason the default build is untouched: the two
configurations never share a CMake cache, so switching between them cannot
half-reconfigure either.

### `cmake_gcc/CMakeLists.txt`

Inside the allowed sections only.

**Application selection** becomes a validated cache variable:

```cmake
set(XBEE_APP_SELECT "PROVISION" CACHE STRING "Which application runs: PROVISION or BRINGUP")
set_property(CACHE XBEE_APP_SELECT PROPERTY STRINGS PROVISION BRINGUP)
```

`PROVISION` is the default, so a bare `cmake` and the existing `project` preset
produce exactly what they produce today. `set_property(... STRINGS ...)` is for
the GUI only and enforces nothing, so the value is checked explicitly and a bad
one is a `message(FATAL_ERROR ...)` at configure time.

**The source list becomes conditional**, which is what physically removes the
unused application:

```cmake
if(XBEE_APP_SELECT STREQUAL "BRINGUP")
    set(app_sources ../src/app/src/xbee_bringup.c)
else()
    set(app_sources ../src/app/src/xbee_provision.c
                    ../src/app/src/xbee_provision_table.c)
endif()
```

`../src/app/src/xbee_dump.c` stays unconditional: it is the shared boot
parameter dump and both applications call it. Every source is still listed
explicitly; no `file(GLOB)`.

**Amended during implementation.** `xbee_dump.c` is listed *ahead* of the
selected application rather than after it. The order of the sources in
`add_executable()` is the link order and decides where the linker places each
function and constant; with the selected application first, the default image
had identical section sizes and an identical symbol set but 165 symbols at
different addresses, and so a different `.bin`. Dump-first reproduces the
historical order with the unselected application removed, and the default image
comes out byte-identical.

**Log level** becomes a second validated cache variable, `XBEE_LOG_LEVEL`,
accepting the level names from `app_log.h`: `VERBOSE`, `INFO`, `WARNING`,
`ERROR`, `NONE`. It is translated to `APP_LOG_LEVEL_MIN=APP_LOG_LEVEL_<name>` on
both targets. `INFO` is the default and is what `app_log_config.h` already
compiles, so this pins today's behaviour rather than changing it, and gives a
place to select `VERBOSE` without editing a header.

The value passed is the `APP_LOG_LEVEL_*` macro name rather than the integer it
expands to, so the build system holds no copy of the numbering that could drift
from `app_log.h`. That is safe because `app_log_config.h` is included from
`app_log.h` alone, below the level definitions.

**Both definitions stay on both targets.** `XBEE_APP`, `XBEE_AT_TABLE_NAMES` and
the new `APP_LOG_LEVEL_MIN` are set on `xbee_provision` and on `slc`, because
`app.c` compiles inside `slc`. For `XBEE_AT_TABLE_NAMES` this is not cosmetic:
it adds a member to `xbee_at_entry_t` under an `#if`
(`src/services/inc/xbee_at_table.h`), so defining it on one target only would be
a silent disagreement about the size of the structure. The existing comment
saying so is preserved; its "roughly 2 kB of flash" figure is corrected to the
4 008 bytes measured in `docs/history/2026-09-19-xbee-parameter-dump.md`
(58 892 minus 54 884).

### `app.c`

`app_init()` selects with `#if / #elif / #else #error`. `app_process_action()`
selects with `#if / #else`, so today a mistyped `XBEE_APP` silently calls the
bring-up application there. That was harmless while both applications were
always compiled in. Once the bring-up source is genuinely absent it would become
an undefined reference at link time, pointing at `app.c` with no explanation.

`app_process_action()` gets the same `#elif / #else #error` shape, so a bad
selection fails at the point of the mistake with the message that names the
valid values. The dispatch itself, and where `XBEE_APP` comes from, are
unchanged; only the diagnostic changes. The comment on `XBEE_APP` is updated to
name the cache variable and the preset rather than a hand edit.

### `.gitignore`

`cmake_gcc/build-provision/` is added. The existing patterns do not cover it:
`cmake_gcc/build/` is an exact directory name and `cmake_gcc/*/build/` matches a
`build` directory one level *below* `cmake_gcc`.

## Simplicity Studio changes required (PRIME RULE)

**None, and none are possible for this change.**

A build preset is not a HAL, pin or component setting. It selects which
application sources the project's own `CMakeLists.txt` compiles and which
preprocessor defines they see. Nothing under `config/`, `autogen/`,
`xbee_provision.slcp`, `xbee_provision.pintool` or the generated
`cmake_gcc/xbee_provision.cmake` is read or written, no peripheral, pin, clock or
software component changes, and both images link against the same unmodified
generated `slc` target.

`cmake_gcc/CMakePresets.json` is the one Studio-owned file this change comes
near, and the whole point of `CMakeUserPresets.json` is that it is not touched:
Studio owns and regenerates `CMakePresets.json`, CMake merges
`CMakeUserPresets.json` on top of it at configure time, and the two never
collide.

## Resource impact

- **Flash: measured at zero, contrary to the expectation above.** The build
  already compiles with `-ffunction-sections -fdata-sections` and links with
  `-Wl,--gc-sections` (`cmake_gcc/xbee_provision.cmake:132,133,175`), so every
  byte of `xbee_bringup.c` was already garbage-collected out of a
  `XBEE_APP_PROVISION` image. The standalone image is byte-identical to the
  default one. Excluding the source removes the application from the build, not
  from the flash budget, because it was not occupying any.
- **RAM:** zero, for the same reason.
- **Peripherals, clocks, energy modes:** unchanged. No runtime code changes; the
  only source edit is a preprocessor diagnostic in `app.c`.
- **Build tree:** one additional build directory, `cmake_gcc/build-provision/`,
  git-ignored.

## Risks

| Risk | Mitigation |
| --- | --- |
| The default image changes as a side effect | `XBEE_APP_SELECT` defaults to `PROVISION` and `XBEE_LOG_LEVEL` to `INFO`, the values already compiled. Verified by comparing `arm-none-eabi-size` and the `.bin` hash against a build made before the change |
| Cross-file preset inheritance is not supported by the installed CMake | Tested explicitly. If it does not work, the fields are duplicated in `CMakeUserPresets.json` rather than the generated file being edited |
| A future Studio generation drops the preset | It cannot: Studio writes `CMakePresets.json` only, and `CMakeUserPresets.json` is a separate tracked file |
| `XBEE_APP_SELECT` and `XBEE_APP` disagree, or a typo selects nothing | The cache variable is validated at configure time, and `app.c` now has an `#error` on both dispatch points |
| A stale cache keeps an old selection | Each selection has its own `binaryDir`; `-DXBEE_APP_SELECT=` on an existing tree reconfigures normally because the source list is evaluated at configure time |

## Test and verification approach

1. `cmake --workflow --preset project` succeeds with zero warnings, and its
   `arm-none-eabi-size` output and `.bin` hash match a build of the parent
   commit made before the change.
2. `cmake --workflow --preset provision` succeeds with zero warnings and
   produces all four artifacts in `build-provision/base/`.
3. `arm-none-eabi-nm` on the standalone `.out` shows no `xbee_bringup_*` symbol.
4. `-DXBEE_APP_SELECT=BRINGUP` on top of the preset builds cleanly, and
   `-DXBEE_APP_SELECT=NONSENSE` fails at configure time with the intended
   message.
5. Host tests still pass: `cmake -S test -B test/build`, build, `ctest`.
6. `git status` clean apart from the intended files;
   `cmake_gcc/CMakeUserPresets.json` tracked and `cmake_gcc/build-provision/`
   ignored.

**No hardware is available for this work.** Verification is build, host tests
and static review only. Neither image is run on a board.
