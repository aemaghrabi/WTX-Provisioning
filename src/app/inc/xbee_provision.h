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
 * @code
 * void app_init(void)           { (void)xbee_provision_init(); }
 * void app_process_action(void) { xbee_provision_process(); }
 * @endcode
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
} xbee_prov_result_t;

/***************************************************************************//**
 * Start provisioning. Call once from app_init().
 *
 * @return SL_STATUS_OK once the sequence has started, or the error that
 *         prevented it. The failure is logged either way.
 ******************************************************************************/
sl_status_t xbee_provision_init(void);

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
