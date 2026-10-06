# CA default 0x32, and write every parameter the audit found different

Status: Done, pending host-test and on-target verification
Date: 2026-10-06
Related history: docs/history/2026-10-06-ca-default-and-audit-writes.md

Implemented as designed, on top of the `xbee provision all` work
(`docs/plan/2026-10-06-cli-provision-all.md`). The firmware builds with no warnings.
`xbee_provision_start()` also documents the new `SL_STATUS_WOULD_OVERFLOW` return.

## Goal

Verification must not fail because a factory default in the command table is wrong.

Observed on target: `E provision: CA reads back as 32, expected 41`. Two facts explain it:
- The module's actual factory default for `CA` is 0x32, as the project owner confirmed.
- The table and `XBEE_PROV_CA` both use 0x41.

Measurable outcome:
- With the default configuration, provisioning passes verification.
- A second run reports "already provisioned, nothing written".

## Background and references

- `docs/manuals/xbee_90002273_ref_manual.md` contradicts itself on the `CA` default:
  - lines 5584 to 5586 (`CA` command reference) give `0x41`;
  - line 4100 (CCA operations) says "the default value of 0x32 (interpreted as -50 dBm)".
  - The module in use reads 0x32 straight after `R1`/`RE`.
- `src/app/src/xbee_provision.c`, `start_next_write()`: after the restore, a parameter is skipped
  when `xbee_at_table_is_default()` says its configured value is the factory default. If the
  table's default is wrong, the parameter is never written. Verification then fails, and the
  audit on the next run sees the same mismatch again.

## Design

1. `src/services/src/xbee_at_table.c`: the `CA` default changes from `0x41U` to `0x32U`. The
   comment cites both manual lines and the observed module value.
2. `src/app/inc/xbee_provision_config.h`: `XBEE_PROV_CA` changes from `0x41U` to `0x32U`, and its
   comment says "Default 0x32". Provisioning now leaves `CA` at the factory value, so there is
   no write for it.
3. `src/app/src/xbee_provision.c`: hardening against any other wrong table default.
   - A bitmap, `audit_mismatch`, has one bit per configuration table index. It is cleared in
     `prepare_run()`.
   - In the audit (not the verification pass), `handle_read_result()` sets the bit when a value
     differs.
   - `start_next_write()` skips a parameter only when it is a table default **and** the audit did
     not find it different. A parameter the audit found different is always written. When the
     table is right, that write is harmless, because the value equals what the restore produced.
   - Capacity is `XBEE_PROV_MAX_PARAMS`, default 128 (16 bytes of RAM). The table has 108
     entries today. `prepare_run()` fails the run with `SL_STATUS_WOULD_OVERFLOW` if the table
     ever outgrows it, rather than silently not tracking.
   - It is still a single `WR`.

## Simplicity Studio changes required (PRIME RULE)

| Tool | Component / instance | Setting | Current value | Required value |
| --- | --- | --- | --- | --- |

None. The change is application code only.

## Resource impact

- **RAM:** 16 bytes (the bitmap).
- **Flash:** a few dozen bytes.
- **Peripherals and energy:** no impact.
- **Module flash:** no impact (one `WR` per run, as before).

## Risks and open questions

- `CA` 0x32 is confirmed for the project owner's module only. A module whose firmware really
  defaults to 0x41 would now be written to 0x32, which matches the configuration, so
  verification still passes.
- The CLI command `no xbee at CA` now restores 0x32.
- The host test `test_report_deviations` prints the write set, and its output no longer lists
  `CA`.

## Verification

1. Firmware build of the CLI variant with zero warnings.
2. Host tests `test_xbee_at_table` and `test_xbee_provision_table` pass. This machine has no host
   compiler, so they are not run here.
3. On target, `xbee provision all`:
   - verification passes;
   - a second run reports "already provisioned, nothing written".
