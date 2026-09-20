/***************************************************************************//**
 * @file
 * @brief Build-time configuration of the transparent UART bridge.
 *
 * Every setting is guarded by #ifndef, so it can be overridden per build with a
 * compiler define instead of editing this file.
 ******************************************************************************/

#ifndef XBEE_BRIDGE_CONFIG_H
#define XBEE_BRIDGE_CONFIG_H

/// Milliseconds to wait after applying the supply before forwarding starts.
///
/// Only the dedicated bridge build waits: entering the bridge from the console
/// leaves a module that is already up. Matches XBEE_POWER_SETTLE_MS, which the
/// facade uses for the same purpose.
#ifndef XBEE_BRIDGE_SETTLE_MS
#define XBEE_BRIDGE_SETTLE_MS  1000U
#endif

/// Milliseconds allowed for an open Command mode session to close on entry.
///
/// The Command mode transport keeps a session open for CT after each command
/// (docs/history/2026-09-19-cmd-session-open-after-transparent-bringup.md), and
/// a tool that finds the module already in Command mode sees its first data
/// answered with ERROR. The bridge drives the facade until the session has gone
/// rather than forwarding into that. If it never does, forwarding starts
/// anyway: a bridge that refuses to open is worse than one that opens late.
#ifndef XBEE_BRIDGE_QUIESCE_TIMEOUT_MS
#define XBEE_BRIDGE_QUIESCE_TIMEOUT_MS  3000U
#endif

/// Capacity of the ring holding terminal bytes on their way to the module.
/// Must be a power of two.
///
/// Eight times the largest transfer that can be in flight, so a full 64-byte
/// transfer shifting out at the configured baud rate never stalls the terminal
/// side. There is no flow control on either link, so this ring and the driver's
/// receive ring are the only back pressure available.
#ifndef XBEE_BRIDGE_TX_RING_SIZE
#define XBEE_BRIDGE_TX_RING_SIZE  512U
#endif

/// Largest number of bytes handed to xbee_uart_write() at once.
///
/// One transfer is in flight at a time, so this bounds how long the ring waits
/// for the next one: 64 bytes is 66.7 ms at 9600 baud.
#ifndef XBEE_BRIDGE_XBEE_CHUNK
#define XBEE_BRIDGE_XBEE_CHUNK  64U
#endif

/// Bytes moved per pass in either direction across the terminal link.
///
/// Matched to SL_IOSTREAM_EUSART_VCOM_RX_BUFFER_SIZE, and deliberately small on
/// the way out: the VCOM transmit path is a per-byte polled loop behind a
/// 16-deep FIFO (platform/service/iostream/src/sl_iostream_uart.c), so a write
/// of this many bytes stalls the super loop for about half of them. Raise it
/// only together with the receive buffer it is sized against.
#ifndef XBEE_BRIDGE_VCOM_CHUNK
#define XBEE_BRIDGE_VCOM_CHUNK  32U
#endif

/// Character that, repeated, leaves the bridge. Ctrl-], as in telnet.
///
/// Not '+': the module's own escape sequence has to reach it untouched
/// (docs/manuals/xbee_90002273_ref_manual.md, lines 3044 to 3051).
#ifndef XBEE_BRIDGE_ESCAPE_CHAR
#define XBEE_BRIDGE_ESCAPE_CHAR  0x1DU
#endif

/// How many times in a row it has to be sent.
#ifndef XBEE_BRIDGE_ESCAPE_COUNT
#define XBEE_BRIDGE_ESCAPE_COUNT  3U
#endif

/// Milliseconds of silence required before and after the sequence.
///
/// The guard windows are what make the escape safe to use on a binary stream:
/// a tool sending frames back to back never leaves a gap this long, so the
/// three characters cannot be mistaken for payload.
#ifndef XBEE_BRIDGE_ESCAPE_GUARD_MS
#define XBEE_BRIDGE_ESCAPE_GUARD_MS  1000U
#endif

#endif  // XBEE_BRIDGE_CONFIG_H
