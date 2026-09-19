/***************************************************************************//**
 * @file
 * @brief Interactive console on the VCOM link, with Cisco IOS command syntax.
 *
 * Selected with XBEE_APP=XBEE_APP_CLI. Brings the XBee module up, then serves a
 * prompt on the debug serial port so that an operator can read and write the
 * module's AT parameters on a live board, with no rebuild and no XCTU.
 *
 * Three modes, as on IOS:
 * @code
 * xbee>                    user EXEC: show commands
 * xbee#                    privileged EXEC: reached with "enable" and a password
 * xbee(config)#            global configuration: reached with "configure terminal"
 * @endcode
 *
 * '?' lists what may come next at any point on the line.
 *
 * Usage, from app.c:
 * @code
 * void app_init(void)           { (void)cli_init(); }
 * void app_process_action(void) { cli_process(); }
 * @endcode
 ******************************************************************************/

#ifndef CLI_H
#define CLI_H

#include "sl_status.h"

/***************************************************************************//**
 * Start the console and begin bringing the XBee module up.
 *
 * Returns as soon as bring-up has been started; the prompt appears once
 * @ref cli_process has driven it to completion.
 *
 * Logging is turned off here, because app_log writes to the same VCOM stream
 * and a log line arriving mid-command would corrupt what the operator is
 * typing. The "logging level" command turns it back on.
 *
 * @return SL_STATUS_OK on success, or the first failure from the console
 *         transport or the XBee facade.
 ******************************************************************************/
sl_status_t cli_init(void);

/***************************************************************************//**
 * Advance the console. Call once per pass of the super loop.
 *
 * Returns promptly in every state. Collects whatever the terminal has sent,
 * advances any request in flight, and never waits for either.
 ******************************************************************************/
void cli_process(void);

#endif  // CLI_H
