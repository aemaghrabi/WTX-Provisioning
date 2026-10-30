/***************************************************************************//**
 * @file
 * @brief The console's "show xbee all" walk over the AT command table.
 *
 * Private to the console. Kept out of cli.c because it is a state machine of
 * its own: it holds one request in flight for several seconds while the rest of
 * the console stays responsive.
 *
 * It owns the single request that is outstanding while it runs, so nothing else
 * may start one until @ref cli_show_all_process reports that it has finished.
 ******************************************************************************/

#ifndef CLI_SHOW_H
#define CLI_SHOW_H

#include <stdbool.h>
#include "sl_status.h"

/***************************************************************************//**
 * Begin walking the command table, printing every readable parameter.
 *
 * @return SL_STATUS_OK when the walk has started,
 *         SL_STATUS_INVALID_STATE if one is already running.
 ******************************************************************************/
sl_status_t cli_show_all_start(void);

/***************************************************************************//**
 * Advance the walk by at most one parameter.
 *
 * @return true while the walk is still running, false once it has finished and
 *         printed its summary.
 ******************************************************************************/
bool cli_show_all_process(void);

/***************************************************************************//**
 * Stop the walk where it stands, on Ctrl-C.
 *
 * Any request already in flight is left to complete into the walk's own request
 * object, which is never looked at again.
 ******************************************************************************/
void cli_show_all_abort(void);

/***************************************************************************//**
 * Print what bring-up learned about the module.
 *
 * Synchronous: it reports the values the facade cached during bring-up and asks
 * the module for nothing.
 ******************************************************************************/
void cli_show_info(void);

/***************************************************************************//**
 * Print the device serial number stored in the USERDATA flash page, for
 * "show device serial-number".
 *
 * Synchronous: one flash read, nothing sent to the module. Prints the value,
 * or "not set" when the first byte of its slot is erased, which is the rule
 * WTX-FW applies, and flags a value that is malformed, has a wrong check digit
 * or lacks its NUL terminator.
 ******************************************************************************/
void cli_show_device_sn(void);

/***************************************************************************//**
 * Print the sensor hardware variant stored in the USERDATA flash page, for
 * "show device sensor-variant".
 *
 * Synchronous: one flash read, nothing sent to the module. Prints the RTD byte
 * and the pressure sensor byte with their meaning. An erased byte is shown as
 * "not set" and noted as not populated, which is how WTX-FW reads it; a byte
 * holding anything this firmware would not write is flagged.
 ******************************************************************************/
void cli_show_sensor_variant(void);

#endif  // CLI_SHOW_H
