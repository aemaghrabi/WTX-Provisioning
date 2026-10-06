# CA default 0x32, and write every parameter the audit found different

Date: 2026-10-06
Related plan: docs/plan/2026-10-06-ca-default-and-audit-writes.md

## Summary

- The `CA` factory default in the command table, and `XBEE_PROV_CA`, change from 0x41 to 0x32.
- The provisioning engine now writes every parameter the audit found different, even when the
  table calls its configured value a default.

## Motivation

The project owner reported `E provision: CA reads back as 32, expected 41` on target, and
confirmed that 0x32 is the correct value for their module.

The manual gives 0x41 in the `CA` command reference (lines 5584 to 5586) and 0x32 in the CCA
operations section (line 4100). Because the table said 0x41, the write pass treated `CA = 0x41`
as a default and skipped it after the restore, so the module kept 0x32 and verification failed.

The engine change keeps any other wrong table default from failing in the same way.

## Changes

| File | Change |
| --- | --- |
| `src/services/src/xbee_at_table.c` | `CA` default `0x41U` -> `0x32U`. Comment cites both manual lines and the observed module value |
| `src/app/inc/xbee_provision_config.h` | `XBEE_PROV_CA` `0x41U` -> `0x32U`. Comment gives the range, the unit and the manual discrepancy |
| `src/app/src/xbee_provision.c` | `audit_mismatch` bitmap (`XBEE_PROV_MAX_PARAMS`, default 128, 16 bytes), cleared in `prepare_run()` and set by the audit on a mismatch. `start_next_write()` skips a default-valued parameter only if the audit did not find it different. `prepare_run()` refuses a table larger than the bitmap |
| `src/app/inc/xbee_provision.h` | `xbee_provision_start()` documents `SL_STATUS_WOULD_OVERFLOW` |
| `docs/plan/2026-10-06-ca-default-and-audit-writes.md` | New plan |
| `docs/history/2026-10-06-ca-default-and-audit-writes.md` | This entry |

The project owner stashed the `xbee provision all` work and the AO change, then committed the `*V`
fix as 76ea8fe. At their request, the stash was popped before this change was applied, so it builds
on that work.

## Simplicity Studio changes (made by the user)

| Tool | Component / instance | Setting | Old value | New value |
| --- | --- | --- | --- | --- |

None.

## Verification

- Firmware build: the CLI variant builds with no warnings under `-Wall -Wextra`.
- Host tests: not run, because there is no host compiler on this machine. No existing test asserts
  the old `CA` default.
- On target: not yet run.

## Known limitations and follow-ups

- `CA` 0x32 is confirmed only for the module in use. A firmware build that really defaults to 0x41
  would be written to 0x32, which still matches the configuration.
- On target: run `xbee provision all` twice. The first run should pass verification and the
  second should report "already provisioned, nothing written".
