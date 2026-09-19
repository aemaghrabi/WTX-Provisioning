# Provisioning no longer prints credentials to the log

Date: 2026-09-19
Related plan: docs/plan/2026-09-19-xbee-parameter-dump.md

## Summary

`xbee_provision` printed the AES encryption key, and the Secure Session and
Bluetooth SRP material, in full hexadecimal to VCOM. It no longer does: those
values now log as a name and a byte count only.

## Motivation

This was live in shipping code, not a hypothetical.

`start_next_write()` in `src/app/src/xbee_provision.c` logged every parameter it
was about to write:

```c
APP_LOG_INFO("writing %s = %s",
             command_name(table[i].command),
             value_text(text, table[i].value, table[i].len));
```

`KY` is in the provisioning table, and `src/app/inc/xbee_provision_config.h`
line 198 carries a real 16-byte key. Every provisioning run that wrote the
module therefore printed that key, in the clear, to a serial port that anybody
with a probe on PD08 could read.

The audit and verification passes had the same exposure for the readable
credentials. `*S *V *W *X *Y` and `$S $V $W $X $Y` are readable and
provisionable, so the mismatch line `"%s is %s, configured as %s"` and the
verification failure line `"%s reads back as %s, expected %s"` would have
printed them both as read and as configured.

## Changes

| File | Change |
| --- | --- |
| `src/app/src/xbee_provision.c` | `value_text()` takes the command and masks a credential; the private hexadecimal formatting moves to `byte_util`; `command_name()` takes a caller buffer |
| `src/app/inc/xbee_dump.h`, `src/app/src/xbee_dump.c` | `xbee_dump_is_secret()`, the one definition of what a credential is |
| `src/utils/inc/byte_util.h`, `src/utils/src/byte_util.c` | New `byte_util_hex_text()` |
| `test/test_byte_util.c` | Covers the new helper, including the 16-byte case that used to be a key printed in full |

### What counts as a credential

`xbee_dump_is_secret()` is true for:

- `KY`, the AES Encryption Key (manual lines 5415 to 5491)
- `*S *V *W *X *Y`, the Secure Session salt and verifier (manual lines 5492 to
  5539)
- `$S $V $W $X $Y`, the Bluetooth SRP salt and verifier (manual lines 5917 to
  5994)

There is one definition, in the dump module, used by both the dump and the
provisioning write path. A credential added to the command set in future is
masked in both places or in neither, and cannot be masked in one and forgotten
in the other.

### What the log says now

A masked value logs as its width, which is what a reader actually needs: it says
whether a credential is set without saying what it is.

```
[     9.412] I provision: writing KY = (16 bytes, not logged)
[    12.510] I dump: KY (AES Encryption Key) = (write only, 16 bytes)
[    12.548] I dump: *S (Secure Session Salt) = (4 bytes, not logged)
```

### The shared static buffer in `command_name()`

Fixed in the same pass, because it is next to the same lines.
`command_name()` returned a shared `static char[4]`, so two command names in one
`printf` would have resolved to the same buffer and printed the same name twice.
No call site did that today, but `handle_read_result()` already prints two
values side by side, and the next one to print two names would have been a
quiet, wrong log line rather than a compile error. It now takes a caller buffer,
like the value formatter beside it, and all five call sites were updated.

## Simplicity Studio changes (made by the user)

None. Application code only.

## Verification

**Build, host tests and static review. Nothing was run on hardware.**

- Host tests: 8 suites, all passing. `test_byte_util` exercises the 16-byte
  value that used to be the key printed in full.
- Target build clean, zero warnings.
- Static review: every `value_text()` call site in `xbee_provision.c` now passes
  the command, so masking cannot be bypassed by a caller that forgets it. The
  three call sites are the write line, the audit mismatch line and the
  verification failure line, which were the three places a credential could
  reach the log.
- Grepped for any other log line that formats a table value; there is none.

## Known limitations and follow-ups

- The byte count of a credential is still logged. That is deliberate: it
  distinguishes "no key set" from "a key is set" without disclosing the value.
- Masking is by command identifier. A credential stored under a command not on
  the list would not be masked, which is why the list lives in one place with
  the manual line references next to it.
- Nothing was verified on hardware: the change is to what is printed, and the
  printing path itself was not exercised on a board.
