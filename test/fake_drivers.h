/***************************************************************************//**
 * @file
 * @brief Host stand-ins for the XBee control line drivers and the log.
 ******************************************************************************/

#ifndef FAKE_DRIVERS_H
#define FAKE_DRIVERS_H

#include <stdbool.h>
#include <stdint.h>

/// What the control lines are doing.
typedef struct {
  bool powered;              ///< The supply switch is on.
  uint32_t power_on_count;   ///< Times the supply has been applied.
  bool reset_asserted;       ///< The reset line is held low.
  uint32_t reset_pulse_count; ///< Reset pulses started.
  bool sleep_requested;      ///< The sleep request line is asserted.
  bool sleep_status_awake;   ///< What the module's status line reports.
} fake_lines_t;

/***************************************************************************//**
 * Put every line back to its power-on state.
 ******************************************************************************/
void fake_drivers_reset(void);

/***************************************************************************//**
 * The current state of the control lines.
 *
 * @return The lines. Never NULL.
 ******************************************************************************/
const fake_lines_t *fake_drivers_lines(void);

/***************************************************************************//**
 * Set what the module's sleep status line reports.
 *
 * @param[in] awake true for awake, which is the line held high.
 ******************************************************************************/
void fake_drivers_set_awake(bool awake);

/***************************************************************************//**
 * Echo the code under test's log to standard output, for diagnosing a failure.
 *
 * @param[in] echo true to print.
 ******************************************************************************/
void fake_drivers_set_log_echo(bool echo);

#endif  // FAKE_DRIVERS_H
