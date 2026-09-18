/***************************************************************************//**
 * @file
 * @brief Build-time configuration of the xbee facade.
 *
 * Every setting is guarded by #ifndef, so it can be overridden per build with a
 * compiler define instead of editing this file.
 ******************************************************************************/

#ifndef XBEE_CONFIG_H
#define XBEE_CONFIG_H

/// Time allowed for the module to boot after its supply is applied, in
/// milliseconds.
///
/// UNVERIFIED. The boot time after power-up is not given in
/// docs/manuals/xbee_90002273_ref_manual.md; it is in the XBee 3 Hardware
/// Reference Manual, which is not in the repository. One second is generous for
/// a module of this class, and the probe that follows retries anyway.
#ifndef XBEE_POWER_SETTLE_MS
#define XBEE_POWER_SETTLE_MS  1000U
#endif

/// Time allowed for each step of the mode probe, in milliseconds.
///
/// One second at 9600 baud is far longer than an AT Command Response takes, so
/// a step only expires when the module is not answering in that mode at all.
#ifndef XBEE_PROBE_TIMEOUT_MS
#define XBEE_PROBE_TIMEOUT_MS  1000U
#endif

/// Time allowed for the module's status line to report it has fallen asleep.
///
/// Covers the module finishing a transmission or reception first
/// (manual lines 4719 to 4729).
#ifndef XBEE_SLEEP_ENTER_TIMEOUT_MS
#define XBEE_SLEEP_ENTER_TIMEOUT_MS  2000U
#endif

/// Time allowed for the module's status line to report it has woken.
#ifndef XBEE_SLEEP_EXIT_TIMEOUT_MS
#define XBEE_SLEEP_EXIT_TIMEOUT_MS  1000U
#endif

/// Settling time after the module reports itself awake, in milliseconds.
///
/// The manual requires the request line to stay de-asserted for at least two
/// character times after the module signals readiness on CTS
/// (lines 4726 to 4729). CTS is not wired on this board, so readiness is taken
/// from the ON_SLEEP line and this margin stands in for the character times:
/// two bytes at 9600 baud is about 2.1 ms, rounded up for safety.
#ifndef XBEE_SLEEP_WAKE_GUARD_MS
#define XBEE_SLEEP_WAKE_GUARD_MS  5U
#endif

#endif  // XBEE_CONFIG_H
