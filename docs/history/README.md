# Change history

Record of every change made to `xbee_provision`: application code, configuration changes made in
Simplicity Studio, build/tooling, and documentation. Rules: see "Rule: document as you go" in
`CLAUDE.md`.

- File name: `YYYY-MM-DD-<short-kebab-slug>.md`; several changes on one day may share one file.
- Append-only: correct an earlier entry by adding a new entry that references it.
- Copy the template below into a new file.

## Template

```markdown
# <Title>

Date: YYYY-MM-DD
Related plan: docs/plan/<file>.md (or "none")

## Summary
One or two sentences on what changed.

## Motivation
Why the change was needed.

## Changes
| File | Change |
| --- | --- |

## Simplicity Studio changes (made by the user)
| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |

None if no configuration changed.

## Verification
Build only / static review / on target, with results.

## Known limitations and follow-ups
```
