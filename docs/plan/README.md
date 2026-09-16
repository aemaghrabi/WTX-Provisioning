# Plans

Design and implementation plans for `xbee_provision`. A plan is written **before** implementation
starts and kept current until the work is done. Rules: see "Rule: document as you go" in
`CLAUDE.md`.

- File name: `YYYY-MM-DD-<short-kebab-slug>.md`
- Copy the template below into a new file.
- References come from `docs/manuals/`; cite file and section.

## Template

```markdown
# <Title>

Status: Draft | Approved | In progress | Done | Superseded (by <plan file>)
Date: YYYY-MM-DD
Related history: docs/history/<file>.md

## Goal
What the change must achieve, in measurable terms.

## Background and references
- docs/manuals/<file>.md, section <x.y>: <what it establishes>

## Design
States, timing, data structures, interfaces, failure handling and recovery.

## Simplicity Studio changes required (PRIME RULE)
| Tool | Component / instance | Setting | Current value | Required value |
| --- | --- | --- | --- | --- |

None if the change is application code only.

## Resource impact
RAM, flash, peripherals, interrupts, lowest reachable energy mode.

## Risks and open questions

## Verification
Build checks, static review points, on-target tests and expected results.
```
