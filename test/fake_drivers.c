/***************************************************************************//**
 * @file
 * @brief Host stand-ins for the XBee control line drivers and the log.
 *
 * Lets the facade, which is otherwise pure logic over the drivers, be tested on
 * the host. Each control line is a variable the test can read, so a test can
 * check that the module was powered, reset or asked to sleep at the right point.
 *
 * Link this alongside fake_platform.c in place of the three GPIO drivers in
 * src/drivers/src and src/utils/src/app_log.c.
 ******************************************************************************/

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "fake_drivers.h"
#include "app_log.h"
#include "xbee_power.h"
#include "xbee_reset.h"
#include "xbee_sleep.h"

/// State of each line, and how often it has changed.
static fake_lines_t lines;

/// True while a reset pulse is running.
static bool pulse_busy;

/// True when the log is echoed to standard output.
static bool log_echo;

/***************************************************************************//**
 * Put every line back to its power-on state.
 ******************************************************************************/
void fake_drivers_reset(void)
{
  (void)memset(&lines, 0, sizeof(lines));
  // The module drives its status line high while it is awake, and the tests
  // start with a module that is awake.
  lines.sleep_status_awake = true;
  pulse_busy = false;
}

/***************************************************************************//**
 * The current state of the control lines.
 ******************************************************************************/
const fake_lines_t *fake_drivers_lines(void)
{
  return &lines;
}

/***************************************************************************//**
 * Set what the module's sleep status line reports.
 ******************************************************************************/
void fake_drivers_set_awake(bool awake)
{
  lines.sleep_status_awake = awake;
}

/***************************************************************************//**
 * Echo the code under test's log to standard output.
 ******************************************************************************/
void fake_drivers_set_log_echo(bool echo)
{
  log_echo = echo;
}

// ---------------------------------------------------------------------------
// Logging.

sl_status_t app_log_write(app_log_level_t level, const char *tag,
                          const char *format, ...)
{
  va_list args;

  if (!log_echo) {
    return SL_STATUS_OK;
  }

  (void)printf("    [%u] %s: ", (unsigned)level, (tag != NULL) ? tag : "-");
  va_start(args, format);
  (void)vprintf(format, args);
  va_end(args);
  (void)printf("\n");

  return SL_STATUS_OK;
}

sl_status_t app_log_set_level(app_log_level_t level)
{
  (void)level;
  return SL_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Supply switch.

sl_status_t xbee_power_init(void)
{
  lines.powered = false;
  return SL_STATUS_OK;
}

sl_status_t xbee_power_on(void)
{
  if (!lines.powered) {
    lines.power_on_count++;
  }
  lines.powered = true;
  return SL_STATUS_OK;
}

sl_status_t xbee_power_off(void)
{
  lines.powered = false;
  return SL_STATUS_OK;
}

sl_status_t xbee_power_is_on(bool *is_on)
{
  if (is_on == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  *is_on = lines.powered;
  return SL_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Reset line.

sl_status_t xbee_reset_init(void)
{
  lines.reset_asserted = false;
  return SL_STATUS_OK;
}

sl_status_t xbee_reset_assert(void)
{
  lines.reset_asserted = true;
  return SL_STATUS_OK;
}

sl_status_t xbee_reset_release(void)
{
  lines.reset_asserted = false;
  return SL_STATUS_OK;
}

sl_status_t xbee_reset_is_asserted(bool *asserted)
{
  if (asserted == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  *asserted = lines.reset_asserted;
  return SL_STATUS_OK;
}

sl_status_t xbee_reset_pulse_start(void)
{
  lines.reset_pulse_count++;
  lines.reset_asserted = true;
  pulse_busy = true;
  return SL_STATUS_OK;
}

sl_status_t xbee_reset_pulse_process(void)
{
  if (!pulse_busy) {
    return SL_STATUS_OK;
  }

  // The pulse is taken to complete on the first poll, so a test does not have
  // to model its width. The width itself belongs to the driver's own tests.
  lines.reset_asserted = false;
  pulse_busy = false;

  return SL_STATUS_OK;
}

bool xbee_reset_pulse_busy(void)
{
  return pulse_busy;
}

// ---------------------------------------------------------------------------
// Sleep lines.

sl_status_t xbee_sleep_init(void)
{
  lines.sleep_requested = false;
  return SL_STATUS_OK;
}

sl_status_t xbee_sleep_request(bool assert)
{
  lines.sleep_requested = assert;
  return SL_STATUS_OK;
}

sl_status_t xbee_sleep_request_is_asserted(bool *asserted)
{
  if (asserted == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  *asserted = lines.sleep_requested;
  return SL_STATUS_OK;
}

sl_status_t xbee_sleep_is_awake(bool *awake)
{
  if (awake == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  *awake = lines.sleep_status_awake;
  return SL_STATUS_OK;
}
