/***************************************************************************//**
 * @file
 * @brief Multi-level logger over the stdio console (VCOM iostream).
 *
 * Line format: "[<sec>.<msec>] <L> <tag>: <message>", where L is V, I, W or E.
 *
 * Usage, in a .c file:
 * @code
 * #define APP_LOG_TAG "xbee_at"
 * #include "app_log.h"
 *
 * APP_LOG_WARNING("no OK, retry %u/%u", retry, APP_XBEE_RETRIES);
 * @endcode
 *
 * - APP_LOG_LEVEL_MIN (app_log_config.h) sets the compile-time floor. Calls
 *   below it are compiled out and their arguments are not evaluated.
 * - app_log_set_level() raises the floor at runtime. It can never restore
 *   levels that were compiled out.
 * - The format argument must be a string literal.
 *
 * @note Call from thread (super-loop) context only. The stdio write blocks, so
 *       calls from an IRQ or a driver callback are dropped and
 *       app_log_write() returns SL_STATUS_INVALID_STATE.
 ******************************************************************************/

#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdint.h>
#include "sl_status.h"

/// @name Log levels (plain integers so they can be compared in #if)
/// @{
#define APP_LOG_LEVEL_VERBOSE  0U
#define APP_LOG_LEVEL_INFO     1U
#define APP_LOG_LEVEL_WARNING  2U
#define APP_LOG_LEVEL_ERROR    3U
#define APP_LOG_LEVEL_NONE     4U  ///< Filter value only: disables all output.
/// @}

/// Log level type, holds one of the APP_LOG_LEVEL_* values.
typedef uint8_t app_log_level_t;

#include "app_log_config.h"

#if (APP_LOG_LEVEL_MIN) > APP_LOG_LEVEL_NONE
#error "APP_LOG_LEVEL_MIN must be one of the APP_LOG_LEVEL_* values"
#endif

/// Module tag printed on each line. Define it before including this header.
#ifndef APP_LOG_TAG
#define APP_LOG_TAG  "-"
#endif

#if APP_LOG_COLOR_ENABLE
#define APP_LOG_COLOR_RESET  "\x1B[0m"
#else
#define APP_LOG_COLOR_RESET  ""
#endif

/***************************************************************************//**
 * Write one log line. Back end of the APP_LOG_* macros; do not call directly.
 *
 * @param[in] level One of APP_LOG_LEVEL_VERBOSE .. APP_LOG_LEVEL_ERROR.
 * @param[in] tag   Module tag.
 * @param[in] fmt   printf-style format, including the trailing newline.
 *
 * @return SL_STATUS_OK if written or filtered out by the runtime level,
 *         SL_STATUS_NULL_POINTER if tag or fmt is NULL,
 *         SL_STATUS_INVALID_PARAMETER if level is out of range,
 *         SL_STATUS_INVALID_STATE if called from IRQ context,
 *         SL_STATUS_FAIL if the console write failed.
 ******************************************************************************/
sl_status_t app_log_write(app_log_level_t level,
                          const char *tag,
                          const char *fmt,
                          ...) __attribute__((format(printf, 3, 4)));

/***************************************************************************//**
 * Set the runtime minimum level.
 *
 * @param[in] level One of APP_LOG_LEVEL_VERBOSE .. APP_LOG_LEVEL_NONE.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_INVALID_PARAMETER if level is out of range,
 *         SL_STATUS_INVALID_RANGE if level is below APP_LOG_LEVEL_MIN.
 *         On error the current level is unchanged.
 ******************************************************************************/
sl_status_t app_log_set_level(app_log_level_t level);

/***************************************************************************//**
 * Get the runtime minimum level.
 *
 * @return Current runtime minimum level.
 ******************************************************************************/
app_log_level_t app_log_get_level(void);

/// @cond INTERNAL
#define APP_LOG_EMIT_(level, fmt, ...)                                   \
  do {                                                                   \
    (void)app_log_write((level), APP_LOG_TAG,                            \
                        fmt APP_LOG_COLOR_RESET "\n", ##__VA_ARGS__);    \
  } while (0)

#define APP_LOG_DISCARD_(level, fmt, ...)                                \
  do {                                                                   \
    if (0) {                                                             \
      (void)app_log_write((level), APP_LOG_TAG,                          \
                          fmt APP_LOG_COLOR_RESET "\n", ##__VA_ARGS__);  \
    }                                                                    \
  } while (0)
/// @endcond

/// @name Log macros
/// @{
#if (APP_LOG_LEVEL_MIN) <= APP_LOG_LEVEL_VERBOSE
#define APP_LOG_VERBOSE(fmt, ...)  APP_LOG_EMIT_(APP_LOG_LEVEL_VERBOSE, fmt, ##__VA_ARGS__)
#else
#define APP_LOG_VERBOSE(fmt, ...)  APP_LOG_DISCARD_(APP_LOG_LEVEL_VERBOSE, fmt, ##__VA_ARGS__)
#endif

#if (APP_LOG_LEVEL_MIN) <= APP_LOG_LEVEL_INFO
#define APP_LOG_INFO(fmt, ...)     APP_LOG_EMIT_(APP_LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#else
#define APP_LOG_INFO(fmt, ...)     APP_LOG_DISCARD_(APP_LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#endif

#if (APP_LOG_LEVEL_MIN) <= APP_LOG_LEVEL_WARNING
#define APP_LOG_WARNING(fmt, ...)  APP_LOG_EMIT_(APP_LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#else
#define APP_LOG_WARNING(fmt, ...)  APP_LOG_DISCARD_(APP_LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#endif

#if (APP_LOG_LEVEL_MIN) <= APP_LOG_LEVEL_ERROR
#define APP_LOG_ERROR(fmt, ...)    APP_LOG_EMIT_(APP_LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#else
#define APP_LOG_ERROR(fmt, ...)    APP_LOG_DISCARD_(APP_LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#endif
/// @}

#endif  // APP_LOG_H
