/***************************************************************************//**
 * @file
 * @brief Build-time configuration of the app_log module.
 *
 * Every setting is guarded by #ifndef, so it can be overridden per build with a
 * compiler define (for example APP_LOG_LEVEL_MIN=0) instead of editing this
 * file. Level values are defined in app_log.h.
 ******************************************************************************/

#ifndef APP_LOG_CONFIG_H
#define APP_LOG_CONFIG_H

/// Compile-time minimum level. Log calls below it are removed from the build.
/// One of APP_LOG_LEVEL_VERBOSE, _INFO, _WARNING, _ERROR, _NONE (no output).
#ifndef APP_LOG_LEVEL_MIN
#define APP_LOG_LEVEL_MIN     APP_LOG_LEVEL_INFO
#endif

/// 1: colour each line by level with ANSI escape codes. 0: plain text.
#ifndef APP_LOG_COLOR_ENABLE
#define APP_LOG_COLOR_ENABLE  1
#endif

#endif  // APP_LOG_CONFIG_H
