/***************************************************************************//**
 * @file
 * @brief Character transport to the operator's terminal over the VCOM iostream.
 *
 * The console's only door to the serial port. Reads never block and writes are
 * pushed out immediately, which is what a super-loop application needs from an
 * interactive link.
 *
 * Usage:
 * @code
 * (void)console_uart_init();
 *
 * char buf[32];
 * uint16_t got = 0U;
 *
 * (void)console_uart_read(buf, sizeof(buf), &got);
 * console_uart_printf("read %u bytes\n", got);
 * @endcode
 *
 * @note Call from thread (super-loop) context only. Writing blocks until the
 *       characters have been handed to the driver, so an IRQ or a driver
 *       callback must not call any of these.
 ******************************************************************************/

#ifndef CONSOLE_UART_H
#define CONSOLE_UART_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

/***************************************************************************//**
 * Prepare the console transport.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NOT_INITIALIZED if the VCOM stream is not available.
 ******************************************************************************/
sl_status_t console_uart_init(void);

/***************************************************************************//**
 * Collect whatever the operator has typed, without waiting.
 *
 * Returns at once with nothing when the terminal has sent nothing, so it is
 * safe to call on every pass of the super loop.
 *
 * @param[out] buf   Destination for the received characters.
 * @param[in]  cap   Capacity of buf in characters, at least 1.
 * @param[out] count Number received, 0 when the terminal is idle.
 *
 * @return SL_STATUS_OK on success, including a read of zero characters,
 *         SL_STATUS_NULL_POINTER if buf or count is NULL,
 *         SL_STATUS_INVALID_PARAMETER if cap is 0,
 *         SL_STATUS_NOT_INITIALIZED if console_uart_init() has not run.
 ******************************************************************************/
sl_status_t console_uart_read(char *buf, uint16_t cap, uint16_t *count);

/***************************************************************************//**
 * Send characters to the terminal and push them out.
 *
 * @param[in] text Characters to send, need not be NUL-terminated.
 * @param[in] len  Number of characters. Zero is accepted and does nothing.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if text is NULL and len is not 0,
 *         SL_STATUS_NOT_INITIALIZED if console_uart_init() has not run,
 *         SL_STATUS_FAIL if the write failed.
 ******************************************************************************/
sl_status_t console_uart_write(const char *text, uint16_t len);

/***************************************************************************//**
 * Send a NUL-terminated string to the terminal and push it out.
 *
 * @param[in] text String to send.
 *
 * @return As @ref console_uart_write.
 ******************************************************************************/
sl_status_t console_uart_puts(const char *text);

/***************************************************************************//**
 * Send formatted text to the terminal and push it out.
 *
 * @param[in] fmt printf-style format. Unlike the logger's macros this takes no
 *                implicit newline, because a prompt must not end with one.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if fmt is NULL,
 *         SL_STATUS_NOT_INITIALIZED if console_uart_init() has not run,
 *         SL_STATUS_FAIL if the write failed.
 ******************************************************************************/
sl_status_t console_uart_printf(const char *fmt, ...)
  __attribute__((format(printf, 1, 2)));

/***************************************************************************//**
 * Push any buffered output out to the terminal.
 *
 * Rarely needed directly: every write above flushes already. See the note in
 * the implementation for why flushing matters at all here.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NOT_INITIALIZED if console_uart_init() has not run,
 *         SL_STATUS_FAIL if the flush failed.
 ******************************************************************************/
sl_status_t console_uart_flush(void);

/***************************************************************************//**
 * Switch the link between console output and a byte-transparent stream.
 *
 * The instance is configured to insert a carriage return before every line
 * feed it sends (SL_IOSTREAM_EUSART_VCOM_CONVERT_BY_DEFAULT_LF_TO_CRLF), which
 * is what a terminal wants and what a binary stream must not have: a frame
 * carrying 0x0A would arrive with a byte the sender never wrote. Binary mode
 * turns the insertion off for as long as it is in force.
 *
 * Entering binary mode flushes stdio first, so that nothing a console write
 * left in the libc buffer is stranded there or emitted into the binary stream
 * later. While binary mode is in force, use @ref console_uart_write_raw and
 * nothing else: the console writes above go through stdio, whose buffering the
 * raw path does not see.
 *
 * @param[in] binary true to stop translating, false to restore the console
 *                   behaviour.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NOT_INITIALIZED if console_uart_init() has not run,
 *         SL_STATUS_FAIL if the flush on the way in failed. The mode is
 *         applied either way: only the flush can fail, and leaving the mode
 *         unchanged because of it would reject every later raw write.
 ******************************************************************************/
sl_status_t console_uart_set_binary(bool binary);

/***************************************************************************//**
 * Send bytes exactly as given, bypassing stdio.
 *
 * For @ref console_uart_set_binary mode only. Going straight to the stream
 * avoids newlib's line-buffer scan and the flush that every console write has
 * to do, which matters when the caller is forwarding a byte stream rather than
 * printing lines. It is safe here only because binary mode implies nothing else
 * is writing to this stream.
 *
 * Blocks until the bytes have been handed to the peripheral. The SDK's write
 * path for this instance is a polled loop behind a 16-deep FIFO, so the caller
 * must keep @p len small enough that the wait fits its own timing budget.
 *
 * @param[in] data Bytes to send.
 * @param[in] len  Number of bytes. Zero is accepted and does nothing.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if data is NULL and len is not 0,
 *         SL_STATUS_INVALID_STATE if binary mode is not in force,
 *         SL_STATUS_NOT_INITIALIZED if console_uart_init() has not run,
 *         or the error the stream reported.
 ******************************************************************************/
sl_status_t console_uart_write_raw(const uint8_t *data, uint16_t len);

#endif  // CONSOLE_UART_H
