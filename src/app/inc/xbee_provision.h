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
 * hosted or standalone run, but only before anything has been written, or at
 * the save decision described below.
 *
 * A hosted run may ask for a review before the write to flash by passing a
 * review callback to xbee_provision_start(). After the parameters have been
 * written, every table entry is read back and handed to the callback, and the
 * run then waits, with the session kept open, until the host calls
 * xbee_provision_decide(). Saving commits, resets and verifies as before.
 * Declining sends nothing to flash and resets the module, which discards the
 * values staged in the session and brings it back on its stored configuration.
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
  XBEE_PROV_RESULT_DECLINED,             ///< The operator declined to save. The module was reset to its stored configuration.
  XBEE_PROV_RESULT_WRITTEN_WITH_DIFFERENCES, ///< Saved and verified, but some parameters differ from the configuration.
  XBEE_PROV_RESULT_FAIL_REVIEW,          ///< The read-back before saving could not be completed. Nothing was saved.
} xbee_prov_result_t;

/// What the read-back before saving found for one parameter.
typedef enum {
  XBEE_PROV_REVIEW_MATCH,       ///< Reads back as configured.
  XBEE_PROV_REVIEW_MISMATCH,    ///< Reads back differently from the configuration.
  XBEE_PROV_REVIEW_UNREADABLE,  ///< The module never reports this parameter, such as KY.
  XBEE_PROV_REVIEW_READ_FAILED, ///< The module did not answer the read.
} xbee_prov_review_t;

/***************************************************************************//**
 * Receives one parameter of the read-back before saving.
 *
 * Called from xbee_provision_process(), once per configuration table entry and
 * in table order.
 *
 * @param[in] command        Two-character command identifier.
 * @param[in] kind           What the read-back found.
 * @param[in] value          Value read back, or NULL for UNREADABLE and
 *                           READ_FAILED.
 * @param[in] len            Length of @p value.
 * @param[in] configured     Value the configuration asks for.
 * @param[in] configured_len Length of @p configured.
 ******************************************************************************/
typedef void (*xbee_prov_review_cb_t)(uint16_t command,
                                      xbee_prov_review_t kind,
                                      const uint8_t *value,
                                      uint16_t len,
                                      const uint8_t *configured,
                                      uint16_t configured_len);

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
 * @param[in] review_cb Called for every parameter read back before the write
 *                      to flash, after which the run waits for
 *                      xbee_provision_decide(). NULL commits without asking,
 *                      as the standalone run does.
 *
 * @return SL_STATUS_OK once the sequence has started,
 *         SL_STATUS_INVALID_STATE if a run is in progress or the configuration
 *         table is empty,
 *         SL_STATUS_NOT_READY if the module is not ready,
 *         SL_STATUS_WOULD_OVERFLOW if the configuration table has more entries
 *         than XBEE_PROV_MAX_PARAMS.
 ******************************************************************************/
sl_status_t xbee_provision_start(xbee_prov_review_cb_t review_cb);

/***************************************************************************//**
 * Ask the run to stop before anything is written to the module.
 *
 * Accepted while the module is being found, the session opened or the
 * configuration read. The run does not stop on the spot: a read in flight is
 * seen through, then the session is closed and the run finishes with
 * XBEE_PROV_RESULT_ABORTED.
 *
 * Also accepted while the run waits for the save decision, where it is the same
 * as xbee_provision_decide(false).
 *
 * @return SL_STATUS_OK if the request was accepted,
 *         SL_STATUS_INVALID_STATE once the restore has started and before the
 *         save decision, during the verification pass, or when no run is in
 *         progress.
 ******************************************************************************/
sl_status_t xbee_provision_abort(void);

/***************************************************************************//**
 * Report whether the run is waiting for the save decision.
 *
 * True only in a run started with a review callback, once every parameter has
 * been read back and handed to it.
 *
 * @return true while xbee_provision_decide() is expected.
 ******************************************************************************/
bool xbee_provision_awaiting_decision(void);

/***************************************************************************//**
 * Number of parameters the read-back before saving found different.
 *
 * @return The count from the most recent read-back. 0 before one has run.
 ******************************************************************************/
uint16_t xbee_provision_review_mismatches(void);

/***************************************************************************//**
 * Save the reviewed configuration to flash, or discard it.
 *
 * Takes effect on a later xbee_provision_process() pass, once any keep-alive
 * request in flight has completed.
 *
 * @param[in] save true to write to flash, reset and verify. false to reset the
 *                 module without writing, discarding the staged values.
 *
 * @return SL_STATUS_OK if the decision was taken,
 *         SL_STATUS_INVALID_STATE if the run is not waiting for one, or one has
 *         already been given.
 ******************************************************************************/
sl_status_t xbee_provision_decide(bool save);

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
