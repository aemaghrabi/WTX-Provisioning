/***************************************************************************//**
 * @file
 * @brief Writes the device serial number and sensor variant to USERDATA in the
 *        provisioning build.
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
 * passed, it formats the serial number from the build inputs below, then
 * writes the sensor variant and the serial number to USERDATA
 * (src/services/inc/mfg_store.h), reading each back. If the run failed, nothing
 * is written.
 *
 * Flash cannot be rewritten in place, so every run first erases the whole
 * USERDATA page, then writes. Flash is erased a page at a time: everything
 * stored in USERDATA is lost, not only an earlier serial number. What the page
 * held is logged before the erase. The sensor variant is written first and the
 * serial number last, so a unit that has a serial number has its variant too.
 *
 * Build inputs, passed as compiler defines (tools/provision.py passes all):
 * - SN_BURNER_FAMILY: the FF product family code, two digits, unquoted, for
 *   example -DSN_BURNER_FAMILY=18. Stringified like the sequence below.
 *   Required in the provisioning build: app.c stops the build without it.
 * - SN_BURNER_SEQUENCE: the SSSSS part, five digits, unquoted, for example
 *   -DSN_BURNER_SEQUENCE=00123. It is turned into a string by the preprocessor,
 *   so a leading zero is never read as an octal number. Required in the
 *   provisioning build: app.c stops the build without it.
 * - SN_BURNER_BUILD_YYWW: optional, the YYWW part from the build host's date,
 *   four digits. Without it the week comes from the compiler's __DATE__, which
 *   only changes when this file is recompiled.
 *
 * - SN_BURNER_RTD: the RTD byte, 0 (absent) or 1 (present), for example
 *   -DSN_BURNER_RTD=1. Required in the provisioning build: app.c stops the
 *   build without it.
 * - SN_BURNER_PRESSURE: the pressure sensor byte, 0 (none) or 1 (1-5VDC), for
 *   example -DSN_BURNER_PRESSURE=1. The reserved Modbus (2) and mV bridge (3)
 *   types stop the build. Required in the provisioning build.
 *
 * Before its final line it logs "sensor variant written: rtd=0xRR
 * pressure=0xPP" once the variant is in flash, which tools/provision.py also
 * checks. It ends with exactly one log line, which tools/provision.py waits for:
 * - "serial number written: FFYYWWSSSSSC"
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
