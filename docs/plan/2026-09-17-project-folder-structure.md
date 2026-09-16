# Application folder structure for new source files

Status: Approved
Date: 2026-09-17
Related history: docs/history/2026-09-17-project-folder-structure.md

## Goal
Set one standard folder layout for all **new** application source and header files, before the
provisioning modules (XBee AT handling, NVM storage, reporting, helpers) are written. Record it
in `CLAUDE.md` and `readme.md` so later work follows it. Existing files are not moved.

## Background and references
- `xbee_provision.slcp` (Studio-owned) lists only `app.c` under `source:` and `.`/`app.h` under
  `include:`. The generator copies these into `cmake_gcc/xbee_provision.cmake`, which is not in git.
- `cmake_gcc/CMakeLists.txt` has `# Add additional sources here` and
  `# Add additional include paths here` sections that CLAUDE.md allows editing.
- No MCU or XBee manual sections apply. This is a repository organisation change only.

## Design

Decisions made by the user: layered layout with `inc/` + `src/` per layer, root folder `src/`,
build registration through `cmake_gcc/CMakeLists.txt`.

```
xbee_provision/
  app.c, app.h, main.c            unchanged, stay in the project root
  config/, autogen/, cmake_gcc/   unchanged, Studio-owned
  src/
    app/        provisioning sequencing / top-level state machine called from app.c
      inc/  src/
    services/   protocol and feature logic (e.g. xbee_at, nvm_store, report)
      inc/  src/
    drivers/    board-level wrappers over SDK APIs (e.g. xbee_power on XBEE_EN_GPIO)
      inc/  src/
    utils/      hardware-independent helpers (e.g. ring_buffer, crc)
      inc/  src/
  docs/                           unchanged
```

Rules:
- Dependency direction: `app` -> `services` -> `drivers` -> SDK. `utils` may be used by any layer
  and depends on no project layer. No upward includes.
- `inc/` holds only a module's public header. Private helpers are `static` in the `.c`, or go in a
  private header in the same layer's `src/`.
- Naming: snake_case module files (`xbee_at.c`, `xbee_at.h`). Public symbols are prefixed with the
  module name (`xbee_at_init()`). Include guard `XBEE_AT_H`. Doxygen file header and 2-space
  indentation, as in `app.h`.
- Headers are included by bare name. Each layer's `inc/` is on the include path.
- Build registration: list every `.c` explicitly under `# Add additional sources here` (no
  `file(GLOB)`). Add a layer's `inc/` under `# Add additional include paths here` when that
  layer gets its first file. Paths are relative to `cmake_gcc/`, e.g.
  `../src/services/src/xbee_at.c`, `../src/services/inc`.
- Empty folders carry a `.gitkeep`. Remove it once the folder has a real file.
- SDK/HAL sources are never copied into `src/`.

`cmake_gcc/CMakeLists.txt` stays unchanged until the first real source file exists.

## Simplicity Studio changes required (PRIME RULE)
None. The `.slcp` is intentionally left unchanged.

## Resource impact
None. No code, RAM, flash, peripheral or energy-mode changes.

## Risks and open questions
- Files registered only in `CMakeLists.txt` do not appear in the `.slcp`. A Studio export or a
  different generator target (non-CMake) would not pick them up.
- A top-level `test/` folder for host-side unit tests is not part of this plan. It can be added
  later on request.

## Verification
- `find src -type d` matches the tree above.
- `git status` shows only the `.gitkeep` files, this plan, the history entry, `CLAUDE.md` and
  `readme.md`. No Studio-owned file is touched.
- Optional sanity build: `cd cmake_gcc && cmake --workflow --preset project`. No source changes,
  so no change in output is expected.
