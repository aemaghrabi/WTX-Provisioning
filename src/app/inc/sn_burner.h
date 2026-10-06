/***************************************************************************//**
 * @file
 * @brief Writes the device serial number to MCU NVM3 in the provisioning build.
 *
 * The XBEE_APP_PROVISION image runs this after the XBee provisioning engine:
 *
 * @code
 * void app_process_action(void)
 * {
 *   xbee_provision_process();
 *   sn_burner_process();
 * }
 * @endcode
 *
 * Once xbee_provision_is_finished(), it runs exactly once. If the XBee run
 * passed, it formats the serial number from the build inputs below, writes it
 * to NVM3 (replacing whatever is stored) and reads it back. If the run failed,
 * nothing is written.
 *
 * Build inputs, passed as compiler defines (tools/provision.py passes both):
 * - SN_BURNER_SEQUENCE: the NNNNN part, five digits, unquoted, for example
 *   -DSN_BURNER_SEQUENCE=00123. It is turned into a string by the preprocessor,
 *   so a leading zero is never read as an octal number. Required in the
 *   provisioning build: app.c stops the build without it.
 * - SN_BURNER_BUILD_YYWW: optional, the YYWW part from the build host's date,
 *   four digits. Without it the week comes from the compiler's __DATE__, which
 *   only changes when this file is recompiled.
 *
 * It ends with exactly one log line, which tools/provision.py waits for:
 * - "serial number written: YYWW-NNNNN-C"
 * - "serial number not written: <reason>"
 ******************************************************************************/

#ifndef SN_BURNER_H
#define SN_BURNER_H

#include <stdbool.h>

/***************************************************************************//**
 * Advance the serial number step. Returns quickly until the XBee run has
 * finished, then writes once and does nothing more.
 ******************************************************************************/
void sn_burner_process(void);

/***************************************************************************//**
 * Report whether the serial number step has finished, either way.
 ******************************************************************************/
bool sn_burner_is_finished(void);

/***************************************************************************//**
 * Report whether the serial number was written and verified.
 ******************************************************************************/
bool sn_burner_passed(void);

#endif  // SN_BURNER_H
