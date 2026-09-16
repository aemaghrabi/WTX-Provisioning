/***************************************************************************//**
 * @file
 * @brief Minimal line logger over the stdio console (VCOM iostream).
 *
 * Output is routed by iostream_retarget_stdio to the default iostream
 * instance. Every APP_LOG() line ends with '\n', so the line-buffered stdout
 * flushes it immediately and the iostream converts it to "\r\n".
 *
 * @note Call from thread (super-loop) context only. Never call from an IRQ
 *       or a driver callback: the stream write blocks until the bytes are
 *       handed to the UART.
 ******************************************************************************/

#ifndef APP_LOG_H
#define APP_LOG_H

#include "sl_status.h"

/***************************************************************************//**
 * Print a formatted string to the console.
 *
 * @param[in] fmt printf-style format string.
 *
 * @return SL_STATUS_OK on success, SL_STATUS_FAIL if the output failed.
 ******************************************************************************/
sl_status_t app_log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/***************************************************************************//**
 * Print one formatted line (a '\n' is appended) to the console.
 ******************************************************************************/
#define APP_LOG(fmt, ...) app_log_printf(fmt "\n", ##__VA_ARGS__)

#endif  // APP_LOG_H
