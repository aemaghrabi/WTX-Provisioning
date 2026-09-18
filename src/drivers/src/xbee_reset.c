/***************************************************************************//**
 * @file
 * @brief Reset line control for the XBee module over XBEE_nRESET.
 ******************************************************************************/

#include <stddef.h>

#include "sl_gpio.h"
#include "sl_sleeptimer.h"
#include "pin_config.h"

#include "xbee_reset.h"

/// Active-low reset input of the module, named XBEE_nRESET in Pin Tool.
static const sl_gpio_t reset_pin = {
  .port = XBEE_nRESET_GPIO_PORT,
  .pin = XBEE_nRESET_GPIO_PIN,
};

/// True while a pulse started by xbee_reset_pulse_start() is still low.
static bool pulse_busy = false;

/// Tick at which the pulse may be released.
static uint32_t pulse_deadline_tick;

/***************************************************************************//**
 * Report whether a deadline has been reached, tolerating tick counter wrap.
 *
 * @param[in] deadline Tick value to compare against.
 *
 * @return true once the current tick is at or past the deadline.
 ******************************************************************************/
static bool tick_reached(uint32_t deadline)
{
  // Signed difference, so the comparison stays correct across the 32-bit wrap
  // as long as the interval is well under half the counter range.
  return ((int32_t)(sl_sleeptimer_get_tick_count() - deadline) >= 0);
}

/***************************************************************************//**
 * Configure XBEE_nRESET as an open-drain output and release the line.
 ******************************************************************************/
sl_status_t xbee_reset_init(void)
{
  pulse_busy = false;

  // Open drain with the output register high, so the pin is released and the
  // module pull-up holds the line. The MCU only ever pulls it low.
  return sl_gpio_set_pin_mode(&reset_pin, SL_GPIO_MODE_WIRED_AND, true);
}

/***************************************************************************//**
 * Hold the module in reset.
 ******************************************************************************/
sl_status_t xbee_reset_assert(void)
{
  return sl_gpio_clear_pin(&reset_pin);
}

/***************************************************************************//**
 * Release the reset line.
 ******************************************************************************/
sl_status_t xbee_reset_release(void)
{
  return sl_gpio_set_pin(&reset_pin);
}

/***************************************************************************//**
 * Report whether the MCU is pulling the reset line low.
 ******************************************************************************/
sl_status_t xbee_reset_is_asserted(bool *asserted)
{
  bool level = false;
  sl_status_t status;

  if (asserted == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  status = sl_gpio_get_pin_output(&reset_pin, &level);
  if (status != SL_STATUS_OK) {
    return status;
  }

  *asserted = !level;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Begin a reset pulse.
 ******************************************************************************/
sl_status_t xbee_reset_pulse_start(void)
{
  sl_status_t status;

  if (pulse_busy) {
    return SL_STATUS_INVALID_STATE;
  }

  status = xbee_reset_assert();
  if (status != SL_STATUS_OK) {
    return status;
  }

  pulse_deadline_tick = sl_sleeptimer_get_tick_count()
                        + sl_sleeptimer_ms_to_tick((uint16_t)XBEE_RESET_PULSE_MS);
  pulse_busy = true;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Advance a reset pulse.
 ******************************************************************************/
sl_status_t xbee_reset_pulse_process(void)
{
  sl_status_t status;

  if (!pulse_busy) {
    return SL_STATUS_OK;
  }
  if (!tick_reached(pulse_deadline_tick)) {
    return SL_STATUS_IN_PROGRESS;
  }

  status = xbee_reset_release();
  if (status != SL_STATUS_OK) {
    // Leave the pulse marked busy so the caller retries the release.
    return status;
  }

  pulse_busy = false;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Report whether a pulse is still running.
 ******************************************************************************/
bool xbee_reset_pulse_busy(void)
{
  return pulse_busy;
}
