/***************************************************************************//**
 * @file
 * @brief Brings the XBee module to the configuration in xbee_provision_config.h.
 *
 * Runs once at boot. It reads the module's current configuration, and if every
 * parameter already matches the header it stops there, so a board that has been
 * provisioned is not written again. Otherwise it restores the module to its
 * defaults, writes only the parameters the header sets away from the default,
 * commits them with one write to flash, resets the module and reads everything
 * back to prove the configuration took.
 *
 * The whole sequence runs inside one Command mode session, which is what makes
 * a single flash write possible: parameters set in Command mode are staged and
 * do not take effect until the session ends (manual lines 3101 to 3110), so
 * even changes that would break the serial link, such as the API mode or the
 * baud rate, can be written safely and applied together at the reset.
 *
 * Both calls return quickly; the sequence is a state machine.
 *
 * Standalone, at boot, the module starts the facade and drives it:
 *
 * @code
 * void app_init(void)           { (void)xbee_provision_init(); }
 * void app_process_action(void) { xbee_provision_process(); }
 * @endcode
 *
 * Hosted, for example by the console's "xbee provision all", the host already
 * runs the facade and keeps calling xbee_process() itself. It starts a run with
 * xbee_provision_start() once the module is ready, calls
 * xbee_provision_process() on every pass until xbee_provision_is_finished(),
 * and issues no request of its own in between. xbee_provision_abort() stops a
 * hosted or standalone run, but only before anything has been written.
 *
 * @note One write to flash per provisioning run, and none at all when the
 *       module already matches. The module's flash supports 10 000 erase and
 *       write cycles (manual lines 7089 to 7099).
 ******************************************************************************/

#ifndef XBEE_PROVISION_H
#define XBEE_PROVISION_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

/// How a provisioning run ended.
typedef enum {
  XBEE_PROV_RESULT_RUNNING,              ///< Not finished yet.
  XBEE_PROV_RESULT_ALREADY_PROVISIONED,  ///< The module already matched. Nothing was written.
  XBEE_PROV_RESULT_WRITTEN,              ///< Written, committed and verified.
  XBEE_PROV_RESULT_FAIL_BRINGUP,         ///< The module did not answer at all.
  XBEE_PROV_RESULT_FAIL_SESSION,         ///< Command mode could not be opened.
  XBEE_PROV_RESULT_FAIL_AUDIT,           ///< The current configuration could not be read.
  XBEE_PROV_RESULT_FAIL_RESTORE,         ///< The module refused to restore its defaults.
  XBEE_PROV_RESULT_FAIL_WRITE,           ///< The module refused a parameter.
  XBEE_PROV_RESULT_FAIL_COMMIT,          ///< The write to flash failed.
  XBEE_PROV_RESULT_FAIL_RESET,           ///< The module did not come back as configured.
  XBEE_PROV_RESULT_FAIL_VERIFY,          ///< A parameter did not read back as written.
  XBEE_PROV_RESULT_ABORTED,              ///< Stopped by the operator before anything was written.
} xbee_prov_result_t;

/***************************************************************************//**
 * Start provisioning. Call once from app_init().
 *
 * @return SL_STATUS_OK once the sequence has started, or the error that
 *         prevented it. The failure is logged either way.
 ******************************************************************************/
sl_status_t xbee_provision_init(void);

/***************************************************************************//**
 * Start provisioning on a facade the caller already runs.
 *
 * Does not call xbee_init(), and xbee_provision_process() then leaves
 * xbee_process() to the caller. The module must be ready. A run may be started
 * again once the previous one has finished.
 *
 * @return SL_STATUS_OK once the sequence has started,
 *         SL_STATUS_INVALID_STATE if a run is in progress or the configuration
 *         table is empty,
 *         SL_STATUS_NOT_READY if the module is not ready,
 *         SL_STATUS_WOULD_OVERFLOW if the configuration table has more entries
 *         than XBEE_PROV_MAX_PARAMS.
 ******************************************************************************/
sl_status_t xbee_provision_start(void);

/***************************************************************************//**
 * Ask the run to stop before anything is written to the module.
 *
 * Accepted only while the module is being found, the session opened or the
 * configuration read. The run does not stop on the spot: a read in flight is
 * seen through, then the session is closed and the run finishes with
 * XBEE_PROV_RESULT_ABORTED.
 *
 * @return SL_STATUS_OK if the request was accepted,
 *         SL_STATUS_INVALID_STATE once the restore has started, during the
 *         verification pass, or when no run is in progress.
 ******************************************************************************/
sl_status_t xbee_provision_abort(void);

/***************************************************************************//**
 * Advance the sequence. Call once per super-loop iteration.
 ******************************************************************************/
void xbee_provision_process(void);

/***************************************************************************//**
 * Report whether the sequence has finished, either way.
 *
 * @return true once it has stopped.
 ******************************************************************************/
bool xbee_provision_is_finished(void);

/***************************************************************************//**
 * Report whether the module ended up configured as the header describes.
 *
 * @return true when the module was already provisioned or was provisioned
 *         successfully.
 ******************************************************************************/
bool xbee_provision_passed(void);

/***************************************************************************//**
 * How the run ended, for a reporting layer.
 *
 * @return The result. XBEE_PROV_RESULT_RUNNING until the sequence stops.
 ******************************************************************************/
xbee_prov_result_t xbee_provision_get_result(void);

/***************************************************************************//**
 * Text for a result, for a log or a report.
 *
 * @param[in] result The result.
 *
 * @return A description. Never NULL.
 ******************************************************************************/
const char *xbee_provision_result_str(xbee_prov_result_t result);

#endif  // XBEE_PROVISION_H
