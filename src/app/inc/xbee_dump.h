/***************************************************************************//**
 * @file
 * @brief Reads every readable AT parameter from the module and logs it.
 *
 * The diagnostic build for a module that talks but is not configured as
 * expected: it brings the module up, walks the whole AT command table, and
 * writes one line per parameter to the log, decoded to the type the command
 * table records and grouped under the manual's command categories.
 *
 * It writes nothing. No parameter is set, nothing is committed to flash, and
 * the module is not reset. The commands that would act rather than answer are
 * not even asked for; see xbee_dump_format.h.
 *
 * Selected with XBEE_APP set to XBEE_APP_DUMP in cmake_gcc/CMakeLists.txt.
 * Plan: docs/plan/2026-09-20-xbee-at-dump-app.md.
 *
 * @code
 * (void)xbee_dump_init();
 * while (true) {
 *   xbee_dump_process();
 * }
 * @endcode
 ******************************************************************************/

#ifndef XBEE_DUMP_H
#define XBEE_DUMP_H

#include <stdbool.h>
#include "sl_status.h"

/// How the dump ended.
typedef enum {
  XBEE_DUMP_RESULT_RUNNING,       ///< Still working.
  XBEE_DUMP_RESULT_COMPLETE,      ///< The table was walked to the end.
  XBEE_DUMP_RESULT_FAIL_BRINGUP,  ///< The module never answered.
} xbee_dump_result_t;

/***************************************************************************//**
 * Start the dump.
 *
 * Powers and detects the module, then begins reading. Call xbee_dump_process()
 * from the super loop afterwards.
 *
 * @return SL_STATUS_OK when the sequence started, otherwise the reason it could
 *         not, in which case the result is already XBEE_DUMP_RESULT_FAIL_BRINGUP.
 ******************************************************************************/
sl_status_t xbee_dump_init(void);

/***************************************************************************//**
 * Advance the dump.
 *
 * Returns quickly. Every wait is a state machine step, never a blocking delay.
 * Safe to keep calling once the dump has finished: it then only keeps the
 * receive path drained.
 ******************************************************************************/
void xbee_dump_process(void);

/***************************************************************************//**
 * Report whether the dump has stopped, for whatever reason.
 ******************************************************************************/
bool xbee_dump_is_finished(void);

/***************************************************************************//**
 * How the dump ended, or XBEE_DUMP_RESULT_RUNNING while it has not.
 ******************************************************************************/
xbee_dump_result_t xbee_dump_get_result(void);

#endif  // XBEE_DUMP_H
