/***************************************************************************//**
 * @file
 * @brief Bring-up sequence for the XBee module.
 ******************************************************************************/

#define APP_LOG_TAG  "bringup"

#include <stdbool.h>

#include "app_log.h"
#include "xbee.h"
#include "xbee_dump.h"
#include "xbee_uart.h"
#include "xbee_bringup.h"

/// Sequence state.
typedef enum {
  BRINGUP_IDLE,     ///< Not started.
  BRINGUP_WAITING,  ///< The facade is probing and reading parameters.
  BRINGUP_DUMPING,  ///< Logging every parameter the module will report.
  BRINGUP_PASSED,   ///< The module answered and was read.
  BRINGUP_FAILED,   ///< It did not.
} bringup_state_t;

/// Current state.
static bringup_state_t state = BRINGUP_IDLE;

/***************************************************************************//**
 * Name of a serial mode, for the log.
 ******************************************************************************/
static const char *mode_name(xbee_mode_t mode)
{
  const char *name;

  switch (mode) {
    case XBEE_MODE_TRANSPARENT: name = "transparent"; break;
    case XBEE_MODE_API1:        name = "API 1, unescaped"; break;
    case XBEE_MODE_API2:        name = "API 2, escaped"; break;
    default:                    name = "unknown"; break;
  }

  return name;
}

/***************************************************************************//**
 * Log data received over the air, in any mode.
 ******************************************************************************/
static void on_data(uint64_t addr64,
                    uint16_t addr16,
                    uint8_t options,
                    const uint8_t *data,
                    uint16_t len,
                    void *user)
{
  (void)data;
  (void)user;

  APP_LOG_INFO("received %u bytes from %08lX%08lX/%04X, options 0x%02X",
               (unsigned)len,
               (unsigned long)(addr64 >> 32),
               (unsigned long)(addr64 & 0xFFFFFFFFU),
               (unsigned)addr16,
               (unsigned)options);
}

/***************************************************************************//**
 * Log a change in the module's status.
 ******************************************************************************/
static void on_modem_status(xbee_modem_status_t status, void *user)
{
  (void)user;

  APP_LOG_INFO("modem status 0x%02X, %s",
               (unsigned)status, xbee_modem_status_str(status));
}

/***************************************************************************//**
 * Log what bring-up learned that the parameter dump does not print.
 *
 * The parameters bring-up read are deliberately not restated here: the dump
 * that follows prints every value the module will report, and a boot log should
 * not carry the same value twice. What stays is the phase marker, the judgement
 * about the output options, and the serial link counters, which are driver
 * statistics rather than module parameters.
 ******************************************************************************/
static void report_success(void)
{
  xbee_info_t info;
  xbee_uart_stats_t uart;

  if (xbee_get_info(&info) != SL_STATUS_OK) {
    APP_LOG_ERROR("ready but no parameters were read");
    return;
  }

  APP_LOG_INFO("module ready in %s mode", mode_name(xbee_get_mode()));
  // The module ships with AO set to 2, which emits the legacy receive frames
  // rather than the modern ones.
  if (info.ao == 2U) {
    APP_LOG_WARNING("output options 2: legacy receive frames are in use");
  }

  if (xbee_uart_get_stats(&uart) == SL_STATUS_OK) {
    APP_LOG_INFO("serial link: %lu received, %lu sent, %lu overruns, %lu errors",
                 (unsigned long)uart.rx_bytes,
                 (unsigned long)uart.tx_bytes,
                 (unsigned long)uart.rx_overruns,
                 (unsigned long)uart.rx_errors);
    if (uart.rx_overruns > 0U) {
      APP_LOG_WARNING("receive ring overran: bytes were lost");
    }
  }
}

/***************************************************************************//**
 * Log why bring-up did not succeed.
 ******************************************************************************/
static void report_failure(void)
{
  xbee_uart_stats_t uart;

  APP_LOG_ERROR("module bring-up failed, status 0x%04X, facade state %u",
                (unsigned)xbee_get_result(), (unsigned)xbee_get_state());

  if (xbee_uart_get_stats(&uart) == SL_STATUS_OK) {
    APP_LOG_ERROR("serial link: %lu received, %lu sent, %lu overruns, %lu errors",
                  (unsigned long)uart.rx_bytes,
                  (unsigned long)uart.tx_bytes,
                  (unsigned long)uart.rx_overruns,
                  (unsigned long)uart.rx_errors);
    if (uart.rx_bytes == 0U) {
      // Nothing at all came back, so the problem is before the protocol.
      APP_LOG_ERROR("nothing received: check power, wiring and baud rate");
    }
  }
}

/***************************************************************************//**
 * Start the sequence.
 ******************************************************************************/
sl_status_t xbee_bringup_init(void)
{
  static const xbee_config_t config = {
    .mode = XBEE_MODE_AUTO,
    .power_cycle = true,
    .settle_ms = 0U,         // use the configured default
    .probe_timeout_ms = 0U,  // use the configured default
  };
  sl_status_t status;

  APP_LOG_INFO("starting XBee bring-up, detecting the serial mode");

  (void)xbee_set_data_callback(on_data, NULL);
  (void)xbee_set_modem_status_callback(on_modem_status, NULL);

  status = xbee_init(&config);
  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("could not start bring-up, status 0x%04X", (unsigned)status);
    state = BRINGUP_FAILED;
    return status;
  }

  state = BRINGUP_WAITING;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Advance the sequence.
 ******************************************************************************/
void xbee_bringup_process(void)
{
  if (state == BRINGUP_IDLE) {
    return;
  }

  // The facade must keep running after bring-up so that received data and
  // status changes still reach their callbacks.
  (void)xbee_process();

  if (state == BRINGUP_DUMPING) {
    // The dump must not drive the facade itself, which is why this branch sits
    // below the xbee_process() call above and above the guard below.
    xbee_dump_process();
    if (xbee_dump_is_finished()) {
      state = BRINGUP_PASSED;
    }
    return;
  }

  if (state != BRINGUP_WAITING) {
    return;
  }

  if (xbee_is_ready()) {
    report_success();
    // A dump that cannot start is a diagnostic gap, not a bring-up failure.
    state = (xbee_dump_start() == SL_STATUS_OK) ? BRINGUP_DUMPING
                                                : BRINGUP_PASSED;
    return;
  }

  if (xbee_get_state() == XBEE_STATE_FAILED) {
    state = BRINGUP_FAILED;
    report_failure();
  }
}

/***************************************************************************//**
 * Report whether the sequence has finished.
 *
 * False while the parameter dump is still running, so a caller that waits for
 * this sees a complete log before it acts.
 ******************************************************************************/
bool xbee_bringup_is_finished(void)
{
  return ((state == BRINGUP_PASSED) || (state == BRINGUP_FAILED));
}

/***************************************************************************//**
 * Report whether the module passed bring-up.
 ******************************************************************************/
bool xbee_bringup_passed(void)
{
  return (state == BRINGUP_PASSED);
}
