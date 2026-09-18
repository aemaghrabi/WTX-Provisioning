/***************************************************************************//**
 * @file
 * @brief Build-time configuration of the xbee_reset driver.
 *
 * Every setting is guarded by #ifndef, so it can be overridden per build with a
 * compiler define instead of editing this file.
 ******************************************************************************/

#ifndef XBEE_RESET_CONFIG_H
#define XBEE_RESET_CONFIG_H

/// Length of the XBEE_nRESET low pulse, in milliseconds.
///
/// UNVERIFIED. The minimum RESET low time is not given in
/// docs/manuals/xbee_90002273_ref_manual.md; it is specified in the XBee 3
/// Hardware Reference Manual, which is not in docs/manuals/. 10 ms is chosen as
/// a value comfortably above any plausible CMOS reset requirement. Revisit and
/// cite the figure once that manual is available.
#ifndef XBEE_RESET_PULSE_MS
#define XBEE_RESET_PULSE_MS  10U
#endif

#endif  // XBEE_RESET_CONFIG_H
