/***************************************************************************//**
 * @file
 * @brief Sleep request and sleep status lines of the XBee module.
 ******************************************************************************/

#include <stddef.h>

#include "sl_gpio.h"
#include "pin_config.h"

#include "xbee_sleep.h"

/// DTR/SLEEP_RQ input of the module, named XBEE_SLEEPRQ in Pin Tool.
static const sl_gpio_t sleep_rq_pin = {
  .port = XBEE_SLEEPRQ_GPIO_PORT,
  .pin = XBEE_SLEEPRQ_GPIO_PIN,
};

/// ON_SLEEP output of the module, named XBEE_nSLEEP_Status in Pin Tool.
static const sl_gpio_t sleep_status_pin = {
  .port = XBEE_nSLEEP_Status_GPIO_PORT,
  .pin = XBEE_nSLEEP_Status_GPIO_PIN,
};

/***************************************************************************//**
 * Configure both sleep pins and leave the module awake.
 ******************************************************************************/
sl_status_t xbee_sleep_init(void)
{
  sl_status_t status;

  // Driven low: sleep is not requested, so the module stays awake.
  status = sl_gpio_set_pin_mode(&sleep_rq_pin, SL_GPIO_MODE_PUSH_PULL, false);
  if (status != SL_STATUS_OK) {
    return status;
  }

  // Plain input with no pull: the module drives ON_SLEEP whenever D9=1, and a
  // pull would fight it. The level is undefined while the module is unpowered,
  // which is why the caller must know the module state before trusting it.
  return sl_gpio_set_pin_mode(&sleep_status_pin, SL_GPIO_MODE_INPUT, false);
}

/***************************************************************************//**
 * Drive the sleep request line.
 ******************************************************************************/
sl_status_t xbee_sleep_request(bool assert)
{
  return assert ? sl_gpio_set_pin(&sleep_rq_pin)
                : sl_gpio_clear_pin(&sleep_rq_pin);
}

/***************************************************************************//**
 * Report the level driven on the sleep request line.
 ******************************************************************************/
sl_status_t xbee_sleep_request_is_asserted(bool *asserted)
{
  bool level = false;
  sl_status_t status;

  if (asserted == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  status = sl_gpio_get_pin_output(&sleep_rq_pin, &level);
  if (status != SL_STATUS_OK) {
    return status;
  }

  *asserted = level;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Read the module's ON_SLEEP status line.
 ******************************************************************************/
sl_status_t xbee_sleep_is_awake(bool *awake)
{
  bool level = false;
  sl_status_t status;

  if (awake == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  status = sl_gpio_get_pin_input(&sleep_status_pin, &level);
  if (status != SL_STATUS_OK) {
    return status;
  }

  // ON_SLEEP: high means awake, low means asleep (manual line 4787).
  *awake = level;
  return SL_STATUS_OK;
}
