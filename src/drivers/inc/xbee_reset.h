/***************************************************************************//**
 * @file
 * @brief Reset line control for the XBee module over XBEE_nRESET.
 *
 * XBEE_nRESET (PB03, named in Pin Tool and exported by config/pin_config.h) is
 * the module's active-low reset input. The pin is driven open drain
 * (SL_GPIO_MODE_WIRED_AND): the MCU only ever pulls the line low, and the
 * module's own pull-up returns it high when the MCU releases it. That way a
 * second reset source on the same net, such as a programming header, is never
 * fought.
 *
 * The pulse is non-blocking. Start it, then call the process function from the
 * super loop until it reports completion:
 * @code
 * (void)xbee_reset_pulse_start();
 * // later, once per super-loop iteration
 * if (xbee_reset_pulse_process() == SL_STATUS_OK) {
 *   // line released, the module is booting
 * }
 * @endcode
 *
 * @note A reset reboots the module: it leaves Command mode, and in API mode it
 *       emits a Modem Status frame (0x8A) with status 0x00, hardware reset
 *       (docs/manuals/xbee_90002273_ref_manual.md, lines 8762 to 8802). Callers
 *       must abandon any request that was in flight and flush the receive path.
 * @note The pulse length XBEE_RESET_PULSE_MS is unverified; see
 *       xbee_reset_config.h.
 ******************************************************************************/

#ifndef XBEE_RESET_H
#define XBEE_RESET_H

#include <stdbool.h>
#include "sl_status.h"

#include "xbee_reset_config.h"

/***************************************************************************//**
 * Configure XBEE_nRESET as an open-drain output and release the line.
 *
 * Safe to call again; it re-applies the pin configuration and releases.
 *
 * @return SL_STATUS_OK on success, or the sl_gpio error that prevented it.
 ******************************************************************************/
sl_status_t xbee_reset_init(void);

/***************************************************************************//**
 * Hold the module in reset by pulling XBEE_nRESET low.
 *
 * @return SL_STATUS_OK on success, or the sl_gpio error that prevented it.
 ******************************************************************************/
sl_status_t xbee_reset_assert(void);

/***************************************************************************//**
 * Release XBEE_nRESET, letting the module pull-up take the line high.
 *
 * @return SL_STATUS_OK on success, or the sl_gpio error that prevented it.
 ******************************************************************************/
sl_status_t xbee_reset_release(void);

/***************************************************************************//**
 * Report whether the MCU is currently pulling XBEE_nRESET low.
 *
 * This reads the driven level, not the line: with an open-drain output a
 * released line is reported as not asserted even if something else pulls it low.
 *
 * @param[out] asserted true while the MCU holds the module in reset.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if asserted is NULL,
 *         or the sl_gpio error that prevented the read.
 ******************************************************************************/
sl_status_t xbee_reset_is_asserted(bool *asserted);

/***************************************************************************//**
 * Begin a reset pulse: assert the line and start the timer.
 *
 * @return SL_STATUS_OK once the line is low,
 *         SL_STATUS_INVALID_STATE if a pulse is already in progress,
 *         or the sl_gpio error that prevented it.
 ******************************************************************************/
sl_status_t xbee_reset_pulse_start(void);

/***************************************************************************//**
 * Advance a reset pulse. Call from the super loop until it stops reporting
 * SL_STATUS_IN_PROGRESS.
 *
 * @return SL_STATUS_IN_PROGRESS while the line is still held low,
 *         SL_STATUS_OK once the line has been released, including when no pulse
 *         is in progress,
 *         or the sl_gpio error that prevented the release.
 ******************************************************************************/
sl_status_t xbee_reset_pulse_process(void);

/***************************************************************************//**
 * Report whether a pulse started by xbee_reset_pulse_start() is still running.
 *
 * @return true while the pulse is in progress.
 ******************************************************************************/
bool xbee_reset_pulse_busy(void);

#endif  // XBEE_RESET_H
