# Application folder structure for new source files

Date: 2026-09-17
Related plan: docs/plan/2026-09-17-project-folder-structure.md

## Summary
Set up a layered `src/` folder structure (`app`, `services`, `drivers`, `utils`, each with `inc/`
and `src/`) for new application files. Recorded it as a rule in `CLAUDE.md` and in the
`readme.md` repository layout. Existing files were not moved.

## Motivation
The user asked for a standard folder hierarchy for new project code files, with the current
files left as they are, and for the structure to be written down so it is remembered.

## Changes
| File | Change |
| --- | --- |
| `src/{app,services,drivers,utils}/{inc,src}/.gitkeep` | New empty folder skeleton. |
| `CLAUDE.md` | Added "Rule: application folder structure". Added `src/**` to "Allowed to edit". |
| `readme.md` | Added `src/` layer rows to the "Repository layout" table. |
| `docs/plan/2026-09-17-project-folder-structure.md` | New plan. |
| `docs/history/2026-09-17-project-folder-structure.md` | This entry. |

## Simplicity Studio changes (made by the user)
None.

## Verification
Static review: checked the folder tree with `find src`. No firmware build, since no source or
build file changed.

## Known limitations and follow-ups
- `cmake_gcc/CMakeLists.txt` is unchanged. Sources and include paths are added when the first
  module is created.
- New files registered only in CMake do not appear in the `.slcp`.
