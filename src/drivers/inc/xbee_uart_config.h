/***************************************************************************//**
 * @file
 * @brief Build-time configuration of the xbee_uart driver.
 *
 * Every setting is guarded by #ifndef, so it can be overridden per build with a
 * compiler define instead of editing this file.
 ******************************************************************************/

#ifndef XBEE_UART_CONFIG_H
#define XBEE_UART_CONFIG_H

/// Receive ring capacity in bytes. Must be a power of two.
///
/// Sized well above the largest API frame, 136 bytes for an Explicit
/// Addressing Command Request carrying a full 116-byte payload
/// (docs/manuals/xbee_90002273_ref_manual.md, lines 7707 to 7866 and 4465 to
/// 4560), so that a super-loop iteration that is slow to drain does not lose
/// bytes. There is no RTS/CTS flow control on this board, so the ring is the
/// only back pressure available.
#ifndef XBEE_UART_RX_RING_SIZE
#define XBEE_UART_RX_RING_SIZE  512U
#endif

/// Transmit staging buffer in bytes. Bounds one xbee_uart_write() call.
#ifndef XBEE_UART_TX_BUF_SIZE
#define XBEE_UART_TX_BUF_SIZE   256U
#endif

#endif  // XBEE_UART_CONFIG_H
