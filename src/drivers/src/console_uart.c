/***************************************************************************//**
 * @file
 * @brief Character transport to the operator's terminal over the VCOM iostream.
 *
 * Input and output take deliberately different routes.
 *
 * Input goes straight to the stream with sl_iostream_read(). On this build that
 * call does not block: sl_iostream_uart.c only waits on an event flag under
 * SL_CATALOG_KERNEL_PRESENT, and this is a bare-metal sl_main application with
 * no kernel, so the call drains the receive ring and reports SL_STATUS_EMPTY
 * when there is nothing in it. That is what lets the console poll the terminal
 * once per pass of the super loop without ever stalling it.
 *
 * Output goes through stdio, which iostream_retarget_stdio points at this same
 * stream. Writing directly to the stream instead would race with the logger:
 * app_log_write() is printf(), so its lines sit in the stdio buffer while a
 * direct write would overtake them, and the two would interleave out of order
 * on the terminal whenever logging is switched on.
 *
 * That choice brings one obligation, which is the reason this file exists at
 * all rather than the console calling printf() itself. _isatty() in the SDK's
 * sl_iostream_retarget_stdio.c returns 1 unconditionally, and the component
 * that would call setvbuf(stdout, NULL, _IONBF, 0) is not in this project, so
 * newlib gives stdout line buffering. Every app_log line ends in a newline and
 * so flushes itself; a prompt such as "xbee> " does not, and would sit in the
 * buffer unseen while the operator waited for it. Every write here therefore
 * ends in an explicit flush.
 ******************************************************************************/

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "sl_iostream.h"
#include "sl_iostream_init_eusart_instances.h"
#include "sl_iostream_uart.h"

#include "console_uart.h"

/// The stream the terminal is on, once init has found it.
static sl_iostream_t *console_stream;

/// True while the link carries a byte stream rather than console output.
static bool binary_mode;

/***************************************************************************//**
 * Push buffered output out, mapping the libc result onto a status.
 ******************************************************************************/
static sl_status_t flush_output(void)
{
  return (fflush(stdout) == 0) ? SL_STATUS_OK : SL_STATUS_FAIL;
}

sl_status_t console_uart_init(void)
{
  if (sl_iostream_VCOM_handle == NULL) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  console_stream = sl_iostream_VCOM_handle;
  binary_mode = false;

  return SL_STATUS_OK;
}

sl_status_t console_uart_read(char *buf, uint16_t cap, uint16_t *count)
{
  size_t read = 0U;
  sl_status_t status;

  if ((buf == NULL) || (count == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }
  if (cap == 0U) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (console_stream == NULL) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  *count = 0U;

  status = sl_iostream_read(console_stream, buf, (size_t)cap, &read);
  if (status == SL_STATUS_EMPTY) {
    // Nothing typed. Not a failure, and by far the common case.
    return SL_STATUS_OK;
  }
  if (status != SL_STATUS_OK) {
    return status;
  }

  *count = (uint16_t)read;

  return SL_STATUS_OK;
}

sl_status_t console_uart_write(const char *text, uint16_t len)
{
  size_t written;

  if (len == 0U) {
    return SL_STATUS_OK;
  }
  if (text == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (console_stream == NULL) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  written = fwrite(text, 1U, (size_t)len, stdout);
  if (written != (size_t)len) {
    return SL_STATUS_FAIL;
  }

  return flush_output();
}

sl_status_t console_uart_puts(const char *text)
{
  if (text == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  return console_uart_write(text, (uint16_t)strlen(text));
}

sl_status_t console_uart_printf(const char *fmt, ...)
{
  va_list args;
  int written;

  if (fmt == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (console_stream == NULL) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  va_start(args, fmt);
  written = vprintf(fmt, args);
  va_end(args);

  if (written < 0) {
    return SL_STATUS_FAIL;
  }

  return flush_output();
}

sl_status_t console_uart_flush(void)
{
  if (console_stream == NULL) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  return flush_output();
}

sl_status_t console_uart_set_binary(bool binary)
{
  sl_status_t status = SL_STATUS_OK;

  if (console_stream == NULL) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  if (binary) {
    // Anything a console write left in the libc buffer belongs to the console,
    // not to the stream that is about to start. Push it out before the change.
    // Its result is reported but not acted on: the mode change itself cannot
    // fail, and refusing it because of a stale flush would leave the caller in
    // a state where every raw write is rejected and nothing says why.
    status = flush_output();
  }

  sl_iostream_uart_set_auto_cr_lf(sl_iostream_uart_VCOM_handle, !binary);
  binary_mode = binary;

  return status;
}

sl_status_t console_uart_write_raw(const uint8_t *data, uint16_t len)
{
  if (len == 0U) {
    return SL_STATUS_OK;
  }
  if (data == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (console_stream == NULL) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  if (!binary_mode) {
    // Writing straight to the stream while the console is using stdio would
    // overtake whatever is sitting in the libc buffer, so it is refused rather
    // than allowed to reorder output.
    return SL_STATUS_INVALID_STATE;
  }

  return sl_iostream_write(console_stream, data, (size_t)len);
}
