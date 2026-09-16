# Add "questions get answers only" rule to CLAUDE.md

Date: 2026-09-16
Related plan: none

## Summary
Added the rule "questions get answers only" to `CLAUDE.md`.

## Motivation
Requested by the user: when the user asks a question (question phrasing or a question mark),
Claude answers only and modifies nothing.

## Changes
| File | Change |
| --- | --- |
| `CLAUDE.md` | New section "Rule: questions get answers only". Read-only investigation to answer is allowed; any suggested change is given as a recommendation and waits for an explicit instruction. |

## Simplicity Studio changes (made by the user)
None.

## Verification
Static review only. No firmware build; no source code changed.

## Known limitations and follow-ups
None.
