/***************************************************************************//**
 * @file
 * @brief Supply control for the XBee module over XBEE_EN_GPIO.
 *
 * XBEE_EN_GPIO (PA00, named in Pin Tool and exported by config/pin_config.h)
 * drives the load switch that supplies the XBee module: high powers the module,
 * low removes its supply.
 *
 * The module starts unpowered: xbee_power_init() configures the pin as a
 * push-pull output driven low, so a reset of the MCU always leaves the module
 * off until the application asks for it.
 *
 * @note No call here blocks or waits. The module needs time to boot after the
 *       supply comes up; that delay belongs to the caller, which owns a
 *       sleeptimer-based state machine.
 ******************************************************************************/

#ifndef XBEE_POWER_H
#define XBEE_POWER_H

#include <stdbool.h>
#include "sl_status.h"

/***************************************************************************//**
 * Configure XBEE_EN_GPIO as a push-pull output and remove the module supply.
 *
 * Safe to call again; it re-applies the pin configuration and powers down.
 *
 * @return SL_STATUS_OK on success, or the sl_gpio error that prevented it.
 ******************************************************************************/
sl_status_t xbee_power_init(void);

/***************************************************************************//**
 * Apply the module supply.
 *
 * @return SL_STATUS_OK on success, or the sl_gpio error that prevented it.
 ******************************************************************************/
sl_status_t xbee_power_on(void);

/***************************************************************************//**
 * Remove the module supply.
 *
 * @return SL_STATUS_OK on success, or the sl_gpio error that prevented it.
 ******************************************************************************/
sl_status_t xbee_power_off(void);

/***************************************************************************//**
 * Report the level currently driven on XBEE_EN_GPIO.
 *
 * @param[out] is_on true when the supply is applied.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if is_on is NULL,
 *         or the sl_gpio error that prevented the read.
 ******************************************************************************/
sl_status_t xbee_power_is_on(bool *is_on);

#endif  // XBEE_POWER_H
