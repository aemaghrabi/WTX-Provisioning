/***************************************************************************//**
 * @file
 * @brief Byte transport to the XBee module over the UARTDRV XBEE instance.
 *
 * Wraps sl_uartdrv_eusart_XBEE_handle (EUSART0, 9600 baud, 8N1, no flow
 * control) in a stream interface: bytes in, bytes out, nothing protocol
 * specific. The XBee frame and Command mode services sit on top.
 *
 * Receive path: two one-byte UARTDRV receive operations are kept queued at all
 * times. Each completion callback, which runs in interrupt context, pushes the
 * byte into a ring buffer and re-queues its own slot, so the DMA never idles
 * between bytes. UARTDRV_Receive() only completes when the requested count has
 * arrived, which is why the count is one: XBee replies are variable length and
 * the driver must surface each byte as it lands.
 *
 * Transmit path: one staging buffer and one operation in flight. The caller
 * hands over a complete frame or command line, which is copied so the caller's
 * buffer can be reused immediately.
 *
 * @note This module does not initialise the peripheral. Simplicity Studio's
 *       sl_uartdrv_init_instances() already ran from sl_driver_init(), before
 *       app_init().
 * @note There is no RTS/CTS flow control on this board
 *       (docs/history/2026-09-18-eusart-flow-control-off.md), so the module
 *       cannot throttle the MCU and the MCU cannot throttle the module. Drain
 *       the receive side promptly and keep transmissions short.
 ******************************************************************************/

#ifndef XBEE_UART_H
#define XBEE_UART_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

#include "xbee_uart_config.h"

/// Transport counters, for diagnostics and self-test reporting.
typedef struct {
  uint32_t rx_bytes;     ///< Bytes pushed into the receive ring.
  uint32_t tx_bytes;     ///< Bytes handed to UARTDRV for transmission.
  uint32_t rx_overruns;  ///< Bytes dropped because the receive ring was full.
  uint32_t rx_errors;    ///< Receive operations that failed, for example framing.
} xbee_uart_stats_t;

/***************************************************************************//**
 * Start the receive path and clear the counters.
 *
 * Queues both receive operations. Safe to call again: it aborts any operation
 * still pending, empties the ring and re-queues.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_FAIL if UARTDRV rejected a receive operation,
 *         or the error reported by the ring buffer setup.
 ******************************************************************************/
sl_status_t xbee_uart_init(void);

/***************************************************************************//**
 * Stop the receive and transmit paths.
 *
 * Aborts every pending UARTDRV operation on the instance. Call this before
 * removing the module supply, so that no receive operation is left pending on
 * a dead line.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_FAIL if UARTDRV rejected the abort.
 ******************************************************************************/
sl_status_t xbee_uart_deinit(void);

/***************************************************************************//**
 * Queue bytes for transmission.
 *
 * The data is copied into the driver's staging buffer, so @p data may be reused
 * as soon as this returns. Only one transmission is in flight at a time.
 *
 * @param[in] data Bytes to send.
 * @param[in] len  Number of bytes, 1 to XBEE_UART_TX_BUF_SIZE.
 *
 * @return SL_STATUS_OK once the transmission is queued,
 *         SL_STATUS_NULL_POINTER if data is NULL,
 *         SL_STATUS_INVALID_PARAMETER if len is 0,
 *         SL_STATUS_WOULD_OVERFLOW if len exceeds the staging buffer,
 *         SL_STATUS_BUSY if a transmission is still in flight,
 *         SL_STATUS_FAIL if UARTDRV rejected the operation.
 ******************************************************************************/
sl_status_t xbee_uart_write(const uint8_t *data, uint16_t len);

/***************************************************************************//**
 * Report whether a transmission is still in flight.
 *
 * @return true while the staging buffer is owned by UARTDRV.
 ******************************************************************************/
bool xbee_uart_tx_busy(void);

/***************************************************************************//**
 * Sleeptimer tick at which the last transmitted byte is expected to have left.
 *
 * Command mode entry needs a guard time of silence on the line into the module
 * (docs/manuals/xbee_90002273_ref_manual.md, lines 3044 to 3051), so it has to
 * know when the line last carried anything, whoever put it there. Keeping this
 * in the driver rather than in one transport means a frame sent by the API
 * transport is accounted for too.
 *
 * The value is the queue time plus the time the bytes take to shift out at the
 * configured baud rate, because a write returns as soon as the transfer is
 * handed to DMA, long before the last byte is on the wire.
 *
 * @return The tick. Zero before anything has been transmitted.
 ******************************************************************************/
uint32_t xbee_uart_last_tx_tick(void);

/***************************************************************************//**
 * Number of received bytes waiting to be read.
 *
 * @return Byte count.
 ******************************************************************************/
uint16_t xbee_uart_rx_available(void);

/***************************************************************************//**
 * Read received bytes.
 *
 * @param[out] dst   Destination buffer.
 * @param[in]  max   Capacity of dst in bytes.
 * @param[out] count Number of bytes copied, 0 when nothing is waiting.
 *
 * @return SL_STATUS_OK on success, including a copy of zero bytes,
 *         SL_STATUS_NULL_POINTER if dst or count is NULL.
 ******************************************************************************/
sl_status_t xbee_uart_read(uint8_t *dst, uint16_t max, uint16_t *count);

/***************************************************************************//**
 * Discard every buffered received byte.
 *
 * Used when the module is reset or when a transport switches mode, so that
 * stale bytes are not parsed as part of the next response.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_uart_rx_flush(void);

/***************************************************************************//**
 * Read the transport counters.
 *
 * @param[out] stats Destination, filled with a consistent snapshot.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if stats is NULL.
 ******************************************************************************/
sl_status_t xbee_uart_get_stats(xbee_uart_stats_t *stats);

/***************************************************************************//**
 * Reset the transport counters to zero.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_uart_clear_stats(void);

#endif  // XBEE_UART_H
