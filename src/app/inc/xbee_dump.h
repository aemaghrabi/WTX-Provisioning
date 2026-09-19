/***************************************************************************//**
 * @file
 * @brief Reads and logs every parameter the XBee module will report.
 *
 * A diagnostic that answers "what is on this module" from a boot log alone. It
 * walks the AT command table, reads every command that holds a readable stored
 * value, and logs one line per parameter. Values the module refuses, and values
 * it never answers, are logged too, so a reader can tell a missing feature from
 * a broken link.
 *
 * Credentials are never printed. The AES key, the Secure Session values and the
 * Bluetooth SRP values are logged as a name and a byte count only, through
 * xbee_dump_is_secret().
 *
 * @code
 * if (xbee_dump_start() == SL_STATUS_OK) {
 *   // once per super-loop iteration, after the caller's own xbee_process()
 *   xbee_dump_process();
 *   if (xbee_dump_is_finished()) {
 *     ...
 *   }
 * }
 * @endcode
 *
 * @note The caller already drives xbee_process() once per super-loop
 *       iteration; this module must not call it. Calling it twice per
 *       iteration would drain the receive path twice and change the timing of
 *       every transport.
 * @note One request is in flight at a time and at most one parameter line is
 *       written per xbee_dump_process() call, so the super loop stays
 *       responsive. The table is never walked to completion inside one call.
 *       Only the call that ends the dump writes more than one line: the reason
 *       it stopped, if any, and the closing summary.
 * @note The dump only reads. It never writes a parameter and never touches
 *       flash.
 ******************************************************************************/

#ifndef XBEE_DUMP_H
#define XBEE_DUMP_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

/***************************************************************************//**
 * Start the dump. Returns at once.
 *
 * The module must be ready: drive xbee_process() until xbee_is_ready(). What
 * happens to the Command mode session is decided here, once:
 *
 * 1. A session is already open, which is the provisioning path: it is adopted
 *    and left open, because its owner still needs it.
 * 2. Transparent mode with no session: one is opened here and closed when the
 *    dump ends.
 * 3. An API mode with no session: none is opened. Forcing one would route every
 *    read through Command mode and add the entry cost of roughly 2.1 seconds.
 *
 * @return SL_STATUS_OK once the dump has started,
 *         SL_STATUS_NOT_READY if the module is not ready,
 *         SL_STATUS_BUSY if a dump is already running,
 *         or the facade error that prevented it.
 ******************************************************************************/
sl_status_t xbee_dump_start(void);

/***************************************************************************//**
 * Advance the dump. Call once per super-loop iteration, after the caller's own
 * xbee_process(). Does nothing once the dump has finished or was never started.
 ******************************************************************************/
void xbee_dump_process(void);

/***************************************************************************//**
 * Report whether the dump has finished, either way.
 *
 * @return true once nothing more will be read or logged.
 ******************************************************************************/
bool xbee_dump_is_finished(void);

/***************************************************************************//**
 * How the dump ended.
 *
 * A non-OK result means the dump was cut short. It is a diagnostic outcome
 * only: no caller should treat it as a failure of whatever it was doing.
 *
 * @return SL_STATUS_OK when every wanted parameter was attempted,
 *         SL_STATUS_TIMEOUT when a bound stopped it,
 *         SL_STATUS_IN_PROGRESS while it is still running,
 *         or the error that stopped it.
 ******************************************************************************/
sl_status_t xbee_dump_get_result(void);

/***************************************************************************//**
 * Report whether a command holds a credential that must never be logged.
 *
 * True for the AES Encryption Key KY (manual lines 5415 to 5491), the Secure
 * Session salt and verifier *S *V *W *X *Y (manual lines 5492 to 5539) and the
 * Bluetooth SRP salt and verifier $S $V $W $X $Y (manual lines 5917 to 5994).
 *
 * One definition, shared by the dump and by the provisioning write path, so a
 * credential added in one place cannot be forgotten in the other.
 *
 * @param[in] command Packed command characters, see the XBEE_AT_* names.
 *
 * @return true when the value must be masked in any log line.
 ******************************************************************************/
bool xbee_dump_is_secret(uint16_t command);

#endif  // XBEE_DUMP_H
