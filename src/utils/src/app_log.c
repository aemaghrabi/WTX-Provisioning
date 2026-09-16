/***************************************************************************//**
 * @file
 * @brief Multi-level logger over the stdio console (VCOM iostream).
 ******************************************************************************/

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include "sl_core.h"
#include "sl_sleeptimer.h"
#include "app_log.h"

/// Milliseconds per second, for the timestamp split.
#define APP_LOG_MS_PER_S  1000U

/// Runtime minimum level. Accessed from thread context only.
static app_log_level_t runtime_level = APP_LOG_LEVEL_MIN;

/// Level letters, indexed by level.
static const char level_letter[APP_LOG_LEVEL_NONE] = { 'V', 'I', 'W', 'E' };

/// Line colour prefixes, indexed by level.
#if APP_LOG_COLOR_ENABLE
static const char *const level_color[APP_LOG_LEVEL_NONE] = {
  "\x1B[90m",  // VERBOSE: grey
  "",          // INFO: terminal default
  "\x1B[33m",  // WARNING: yellow
  "\x1B[31m",  // ERROR: red
};
#else
static const char *const level_color[APP_LOG_LEVEL_NONE] = { "", "", "", "" };
#endif

/***************************************************************************//**
 * Write one log line.
 ******************************************************************************/
sl_status_t app_log_write(app_log_level_t level,
                          const char *tag,
                          const char *fmt,
                          ...)
{
  va_list args;
  uint64_t uptime_ms = 0U;
  int prefix_len;
  int body_len;

  if ((tag == NULL) || (fmt == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }
  if (level >= APP_LOG_LEVEL_NONE) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (level < runtime_level) {
    return SL_STATUS_OK;
  }
  if (CORE_InIrqContext()) {
    return SL_STATUS_INVALID_STATE;
  }

  if (sl_sleeptimer_tick64_to_ms(sl_sleeptimer_get_tick_count64(),
                                 &uptime_ms) != SL_STATUS_OK) {
    uptime_ms = 0U;  // Timestamp is informational: log the line anyway.
  }

  prefix_len = printf("%s[%6lu.%03lu] %c %s: ",
                      level_color[level],
                      (unsigned long)(uptime_ms / APP_LOG_MS_PER_S),
                      (unsigned long)(uptime_ms % APP_LOG_MS_PER_S),
                      level_letter[level],
                      tag);

  va_start(args, fmt);
  body_len = vprintf(fmt, args);
  va_end(args);

  return ((prefix_len < 0) || (body_len < 0)) ? SL_STATUS_FAIL : SL_STATUS_OK;
}

/***************************************************************************//**
 * Set the runtime minimum level.
 ******************************************************************************/
sl_status_t app_log_set_level(app_log_level_t level)
{
  if (level > APP_LOG_LEVEL_NONE) {
    return SL_STATUS_INVALID_PARAMETER;
  }
#if (APP_LOG_LEVEL_MIN) > APP_LOG_LEVEL_VERBOSE
  // Guarded: with a VERBOSE floor this comparison is always false (-Wtype-limits).
  if (level < APP_LOG_LEVEL_MIN) {
    return SL_STATUS_INVALID_RANGE;
  }
#endif

  runtime_level = level;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Get the runtime minimum level.
 ******************************************************************************/
app_log_level_t app_log_get_level(void)
{
  return runtime_level;
}
