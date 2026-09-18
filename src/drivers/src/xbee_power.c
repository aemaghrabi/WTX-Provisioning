/***************************************************************************//**
 * @file
 * @brief Supply control for the XBee module over XBEE_EN_GPIO.
 ******************************************************************************/

#include <stddef.h>

#include "sl_gpio.h"
#include "pin_config.h"

#include "xbee_power.h"

/// Load switch control pin, named XBEE_EN_GPIO in Pin Tool.
static const sl_gpio_t enable_pin = {
  .port = XBEE_EN_GPIO_PORT,
  .pin = XBEE_EN_GPIO_PIN,
};

/***************************************************************************//**
 * Configure XBEE_EN_GPIO and remove the module supply.
 ******************************************************************************/
sl_status_t xbee_power_init(void)
{
  // Driven low: the module stays unpowered until the application asks for it.
  return sl_gpio_set_pin_mode(&enable_pin, SL_GPIO_MODE_PUSH_PULL, false);
}

/***************************************************************************//**
 * Apply the module supply.
 ******************************************************************************/
sl_status_t xbee_power_on(void)
{
  return sl_gpio_set_pin(&enable_pin);
}

/***************************************************************************//**
 * Remove the module supply.
 ******************************************************************************/
sl_status_t xbee_power_off(void)
{
  return sl_gpio_clear_pin(&enable_pin);
}

/***************************************************************************//**
 * Report the level currently driven on XBEE_EN_GPIO.
 ******************************************************************************/
sl_status_t xbee_power_is_on(bool *is_on)
{
  bool level = false;
  sl_status_t status;

  if (is_on == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  status = sl_gpio_get_pin_output(&enable_pin, &level);
  if (status != SL_STATUS_OK) {
    return status;
  }

  *is_on = level;
  return SL_STATUS_OK;
}
