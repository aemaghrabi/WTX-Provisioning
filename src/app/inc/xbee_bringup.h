/***************************************************************************//**
 * @file
 * @brief Bring-up sequence for the XBee module.
 *
 * Powers the module, lets the facade work out which serial mode it is in, and
 * logs what it found: the mode, the firmware and hardware versions, the 64-bit
 * serial number, the maximum payload and the output options.
 *
 * This is the first real user of the XBee stack and the on-target check that
 * every layer works together. It is also the seed of the provisioning
 * self-test: confirming that the module answers and reports an acceptable
 * firmware version is the first thing provisioning has to do.
 *
 * Both calls return quickly; the sequence runs as a state machine.
 ******************************************************************************/

#ifndef XBEE_BRINGUP_H
#define XBEE_BRINGUP_H

#include <stdbool.h>
#include "sl_status.h"

/***************************************************************************//**
 * Start the sequence. Call once from app_init().
 *
 * @return SL_STATUS_OK once bring-up has started, or the error that prevented
 *         it. The failure is logged either way.
 ******************************************************************************/
sl_status_t xbee_bringup_init(void);

/***************************************************************************//**
 * Advance the sequence. Call from app_process_action().
 ******************************************************************************/
void xbee_bringup_process(void);

/***************************************************************************//**
 * Report whether the sequence has finished, whether or not it succeeded.
 *
 * @return true once bring-up has run to a conclusion.
 ******************************************************************************/
bool xbee_bringup_is_finished(void);

/***************************************************************************//**
 * Report whether the module passed bring-up.
 *
 * @return true when the module answered and its parameters were read.
 ******************************************************************************/
bool xbee_bringup_passed(void);

#endif  // XBEE_BRINGUP_H
