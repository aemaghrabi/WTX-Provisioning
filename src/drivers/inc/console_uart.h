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

#endif  // CONSOLE_UART_H
