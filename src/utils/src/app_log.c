/***************************************************************************//**
 * @file
 * @brief Minimal line logger over the stdio console (VCOM iostream).
 ******************************************************************************/

#include <stdarg.h>
#include <stdio.h>

#include "app_log.h"

/***************************************************************************//**
 * Print a formatted string to the console.
 ******************************************************************************/
sl_status_t app_log_printf(const char *fmt, ...)
{
  va_list args;
  int written;

  if (fmt == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  va_start(args, fmt);
  written = vprintf(fmt, args);
  va_end(args);

  return (written < 0) ? SL_STATUS_FAIL : SL_STATUS_OK;
}
