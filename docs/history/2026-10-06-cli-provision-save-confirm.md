# `xbee provision all`: read back and confirm before saving to flash

Date: 2026-10-06
Related plan: docs/plan/2026-10-06-cli-provision-save-confirm.md

## Summary

After writing the parameters, `xbee provision all` now reads every provisioning parameter back
from the module and lists it on the console as `{Parameter}={value}`. It then asks before `WR`
is sent:

- Everything matches: `do you want to save? [N/y]`. Only `y` or `yes` saves.
- Some parameters differ: `<n> parameters did not read back as written. Save anyway? [Y/n]`.
  Enter, `y` or `yes` saves.

Any other answer, Ctrl-C, an overlong line or a 60 s timeout declines. A declined run sends
nothing to flash and resets the module with its reset line, so it comes back on its stored
configuration. The standalone `XBEE_APP_PROVISION` build commits without asking, as before.

## Motivation

Requested by the project owner: show the configuration held in the module's memory and ask for
an explicit `y` before it is saved with `WR`. The behaviour on decline, on mismatches, and the
list format were chosen by the project owner (see the plan).

## Changes

| File | Change |
| --- | --- |
| `src/app/inc/xbee_provision.h` | `xbee_provision_start()` takes a review callback (`xbee_prov_review_cb_t`, NULL = no confirmation). New `xbee_prov_review_t`, `xbee_provision_awaiting_decision()`, `xbee_provision_review_mismatches()`, `xbee_provision_decide()`. New results `DECLINED`, `WRITTEN_WITH_DIFFERENCES`, `FAIL_REVIEW`, appended last. `xbee_provision_abort()` also accepted at the save decision |
| `src/app/src/xbee_provision.c` | New states `PROV_REVIEW`, `PROV_AWAIT_DECISION`, `PROV_DISCARD`. Read-back pass after the writes when a callback is set. Keep-alive `CT` read at half the session window while waiting, so the Command mode timeout cannot apply the staged values. Discard by `xbee_hw_reset()` without `CN`. Up to `XBEE_PROV_MAX_ACCEPTED` (8) mismatched values kept and expected by the post-reset verify. Result texts |
| `src/app/src/cli.c` | Review callback prints `{Parameter}={value}`, `(differs, configured <value>)`, `(not readable)`, `(read failed)`. New state `CLI_STATE_SAVE_CONFIRM` with the prompt and answer rules above, driving the engine while it waits. `% Not saved: ...` for a declined run, which also clears `config_dirty` |
| `src/app/inc/cli_config.h` | New `CLI_SAVE_CONFIRM_TIMEOUT_MS`, 60000 |
| `docs/plan/2026-10-06-cli-provision-save-confirm.md` | New plan. Status -> Done, pending on-target verification, with implementation notes |
| `docs/history/2026-10-06-cli-provision-save-confirm.md` | This entry |

## Simplicity Studio changes (made by the user)

| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |

None.

## Verification

- Build: the CLI variant builds with `cmake --workflow --preset project`, with no warnings.
- `app.c` with `XBEE_APP=XBEE_APP_PROVISION` passes a `-fsyntax-only -Wall -Wextra` compile, using
  the command from `compile_commands.json`. `CMakeLists.txt` was not changed. `xbee_provision.c`
  is the same object in every variant.
- Not run: host tests (none cover `xbee_provision.c` or `cli.c`), and the on-target checks in the
  plan.

## Known limitations and follow-ups

- On-target verification is outstanding.
- The manual does not say whether a query in Command mode returns the staged or the applied
  value (lines 3101 to 3110). If it is the applied value, every changed parameter is listed as
  differing. The first on-target run will show this.
- `?` at the save prompt shows the command help, as it already does at the `[confirm]` prompt.
- Existing comment at `xbee_provision.c` (`XBEE_PROV_FLASH_TIMEOUT_MS`) says restoring defaults
  writes flash; manual lines 7100 to 7106 do not say so. Left unchanged, awaiting a decision.

---

# Answer help at the yes/no prompts, and the flash timeout comment

Date: 2026-10-06
Related plan: docs/plan/2026-10-06-cli-provision-save-confirm.md

## Summary

Closes the two follow-ups listed in the entry above, as requested by the project owner.

- `?` at the provisioning `[confirm]` prompt and at the save prompt no longer prints the command
  help. It lists the accepted answers and what each one does, then shows the question again with
  the answer typed so far. The behaviour was chosen by the project owner.
- The comment on `XBEE_PROV_FLASH_TIMEOUT_MS` no longer says that restoring defaults writes
  flash. It now says that WR writes flash (manual lines 7089 to 7095), that the manual describes
  R1 and RE only as restoring parameters and gives no duration for them (lines 7100 to 7106 and
  7129 to 7130), and that they get the same allowance. The value is unchanged.

## Changes

| File | Change |
| --- | --- |
| `src/app/src/cli.c` | New `print_confirm_question()`, `print_save_question()`, `show_answer_help()`. `LINE_EDIT_HELP` in `CLI_STATE_CONFIRM` or `CLI_STATE_SAVE_CONFIRM` calls `show_answer_help()` |
| `src/app/src/xbee_provision.c` | Comment on `XBEE_PROV_FLASH_TIMEOUT_MS` only |
| `docs/plan/2026-10-06-cli-provision-save-confirm.md` | Implementation note added |
| `docs/history/2026-10-06-cli-provision-save-confirm.md` | This entry |

## Simplicity Studio changes (made by the user)

None.

## Verification

- Build: the CLI variant builds with `cmake --workflow --preset project`, with no warnings.
- Not run on target.

## Known limitations and follow-ups

- On-target verification of the whole feature is still outstanding (see the entry above).
