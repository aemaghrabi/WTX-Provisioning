/***************************************************************************//**
 * @file
 * @brief Sleep request and sleep status lines of the XBee module.
 *
 * Two pins, both named in Pin Tool and exported by config/pin_config.h:
 *
 * - XBEE_SLEEPRQ (PB05) drives the module's DTR/SLEEP_RQ input. The line is
 *   level activated. For pin sleep (SM=1) a high level puts the module to
 *   sleep and a low level wakes it; for cyclic sleep with pin wake (SM=5) a
 *   high to low transition wakes it and it stays awake while the line is low
 *   (docs/manuals/xbee_90002273_ref_manual.md, lines 4719 to 4729 and 4780 to
 *   4788).
 * - XBEE_nSLEEP_Status (PB04) reads the module's ON_SLEEP output: high means
 *   awake, low means asleep (same manual, line 4787).
 *
 * The status line is polled on request. No GPIO interrupt is configured, which
 * keeps every transition visible from the super loop and leaves the external
 * interrupt slots free.
 *
 * @note Both lines only carry meaning when the module is configured for them:
 *       D8=1 routes DTR/SLEEP_RQ, D9=1 routes ON/SLEEP, and SM must be 1 or 5.
 *       This driver cannot know the module's configuration, so the caller is
 *       responsible for checking SM, D8 and D9 before trusting the pins. On a
 *       factory module, SM=0, the request line has no effect.
 ******************************************************************************/

#ifndef XBEE_SLEEP_H
#define XBEE_SLEEP_H

#include <stdbool.h>
#include "sl_status.h"

/***************************************************************************//**
 * Configure both sleep pins and leave the module awake.
 *
 * XBEE_SLEEPRQ becomes a push-pull output driven low, which is the de-asserted
 * level, and XBEE_nSLEEP_Status becomes a plain input, since the module drives
 * it. Safe to call again.
 *
 * @return SL_STATUS_OK on success, or the sl_gpio error that prevented it.
 ******************************************************************************/
sl_status_t xbee_sleep_init(void);

/***************************************************************************//**
 * Drive the sleep request line.
 *
 * @param[in] assert true drives the line high, asking the module to sleep;
 *                   false drives it low, asking it to wake or stay awake.
 *
 * @return SL_STATUS_OK on success, or the sl_gpio error that prevented it.
 ******************************************************************************/
sl_status_t xbee_sleep_request(bool assert);

/***************************************************************************//**
 * Report the level currently driven on the sleep request line.
 *
 * @param[out] asserted true while sleep is being requested.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if asserted is NULL,
 *         or the sl_gpio error that prevented the read.
 ******************************************************************************/
sl_status_t xbee_sleep_request_is_asserted(bool *asserted);

/***************************************************************************//**
 * Read the module's ON_SLEEP status line.
 *
 * @param[out] awake true when the module reports itself awake.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if awake is NULL,
 *         or the sl_gpio error that prevented the read.
 ******************************************************************************/
sl_status_t xbee_sleep_is_awake(bool *awake);

#endif  // XBEE_SLEEP_H
