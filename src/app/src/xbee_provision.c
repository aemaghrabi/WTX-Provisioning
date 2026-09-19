/***************************************************************************//**
 * @file
 * @brief Brings the XBee module to the configuration in xbee_provision_config.h.
 ******************************************************************************/

#define APP_LOG_TAG  "provision"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sl_sleeptimer.h"
#include "sl_uartdrv_eusart_XBEE_config.h"

#include "app_log.h"
#include "byte_util.h"
#include "xbee.h"
#include "xbee_at_table.h"
#include "xbee_dump.h"
#include "xbee_provision_config.h"
#include "xbee_provision_table.h"
#include "xbee_provision.h"

// ---------------------------------------------------------------------------
// The configured link settings must match the ones Simplicity Studio generated
// for the XBEE instance. If they diverge, the module comes back from its reset
// talking at a rate this MCU is not listening at, and neither the verification
// pass nor the product firmware could reach it.

#if SL_UARTDRV_EUSART_XBEE_BAUDRATE == 1200
#define XBEE_PROV_BD_EXPECTED  0U
#elif SL_UARTDRV_EUSART_XBEE_BAUDRATE == 2400
#define XBEE_PROV_BD_EXPECTED  1U
#elif SL_UARTDRV_EUSART_XBEE_BAUDRATE == 4800
#define XBEE_PROV_BD_EXPECTED  2U
#elif SL_UARTDRV_EUSART_XBEE_BAUDRATE == 9600
#define XBEE_PROV_BD_EXPECTED  3U
#elif SL_UARTDRV_EUSART_XBEE_BAUDRATE == 19200
#define XBEE_PROV_BD_EXPECTED  4U
#elif SL_UARTDRV_EUSART_XBEE_BAUDRATE == 38400
#define XBEE_PROV_BD_EXPECTED  5U
#elif SL_UARTDRV_EUSART_XBEE_BAUDRATE == 57600
#define XBEE_PROV_BD_EXPECTED  6U
#elif SL_UARTDRV_EUSART_XBEE_BAUDRATE == 115200
#define XBEE_PROV_BD_EXPECTED  7U
#elif SL_UARTDRV_EUSART_XBEE_BAUDRATE == 230400
#define XBEE_PROV_BD_EXPECTED  8U
#elif SL_UARTDRV_EUSART_XBEE_BAUDRATE == 460800
#define XBEE_PROV_BD_EXPECTED  9U
#elif SL_UARTDRV_EUSART_XBEE_BAUDRATE == 921600
#define XBEE_PROV_BD_EXPECTED  10U
#else
#error "The XBEE instance uses a baud rate the XBee module has no standard code for. \
Set a standard rate in Simplicity Studio, or extend this mapping (manual lines 6076 to 6086)."
#endif

#if XBEE_PROV_BD != XBEE_PROV_BD_EXPECTED
#error "XBEE_PROV_BD does not match the baud rate of the XBEE instance. \
Change both together: the instance in Simplicity Studio, and XBEE_PROV_BD in \
xbee_provision_config.h."
#endif

// The XBEE instance is configured for no parity and one stop bit. These are
// enumerator tokens rather than numbers, so they cannot be compared by the
// preprocessor; changing the instance to anything else means changing these two
// values to match by hand.
_Static_assert(XBEE_PROV_NB == 0U,
               "XBEE_PROV_NB must be 0, no parity, to match the XBEE instance");
_Static_assert(XBEE_PROV_SB == 0U,
               "XBEE_PROV_SB must be 0, one stop bit, to match the XBEE instance");

#if (XBEE_PROV_AP != 0U) && (XBEE_PROV_AP != 1U) && (XBEE_PROV_AP != 2U)
#error "XBEE_PROV_AP must be 0, 1 or 2. Mode 4, API within MicroPython, is not driven here."
#endif

#if (XBEE_PROV_RESTORE != XBEE_PROV_RESTORE_R1) \
    && (XBEE_PROV_RESTORE != XBEE_PROV_RESTORE_RE)
#error "XBEE_PROV_RESTORE must be XBEE_PROV_RESTORE_R1 or XBEE_PROV_RESTORE_RE."
#endif

// ---------------------------------------------------------------------------

/// Time allowed for a command that works on flash, in milliseconds.
///
/// Writing the configuration and restoring defaults both erase and rewrite
/// flash, which takes far longer than a parameter access. The manual forbids
/// sending anything to the module before the write answers (lines 7093 to
/// 7095), so this is generous on purpose.
#ifndef XBEE_PROV_FLASH_TIMEOUT_MS
#define XBEE_PROV_FLASH_TIMEOUT_MS  5000U
#endif

/// Time allowed for the Command mode session to close, in milliseconds.
///
/// If the exit command cannot be sent, the module leaves Command mode by itself
/// once its own timeout lapses, which is 10 seconds by default (manual lines
/// 3062 to 3065). This bounds that wait in this module rather than leaving a
/// reader to work it out from the module's configuration.
#ifndef XBEE_PROV_CLOSE_TIMEOUT_MS
#define XBEE_PROV_CLOSE_TIMEOUT_MS  15000U
#endif

/// Bytes of a parameter value written to the log before it is abbreviated.
#define LOG_VALUE_MAX_BYTES  16U

/// Characters needed to print an abbreviated value: two per byte, an ellipsis
/// and the terminator.
#define VALUE_TEXT_CAP  ((LOG_VALUE_MAX_BYTES * 2U) + 4U)

/// Characters needed for a command name: two characters and the terminator.
#define CMD_TEXT_CAP  3U

/// Where the sequence has got to.
typedef enum {
  PROV_IDLE,       ///< Not started.
  PROV_BRINGUP,    ///< Waiting for the facade to find and read the module.
  PROV_OPEN,       ///< Opening the Command mode session.
  PROV_DUMP,       ///< Logging every parameter the module will report.
  PROV_AUDIT,      ///< Reading the module's configuration to compare it.
  PROV_RESTORE,    ///< Restoring the module's defaults.
  PROV_WRITE,      ///< Writing the parameters that deviate from the default.
  PROV_COMMIT,     ///< Writing the configuration to flash.
  PROV_CLOSE,      ///< Closing the session.
  PROV_RESET,      ///< Resetting the module so the new configuration applies.
  PROV_DONE,       ///< Finished.
} prov_state_t;

/// Current step.
static prov_state_t state = PROV_IDLE;

/// Outcome, once there is one.
static xbee_prov_result_t result = XBEE_PROV_RESULT_RUNNING;

/// Result to record when the session being closed has finished closing.
static xbee_prov_result_t pending_result = XBEE_PROV_RESULT_RUNNING;

/// The one request in flight.
static xbee_at_req_t req;

/// True while a request is outstanding.
static bool req_active;

/// Position in the configuration table while reading or writing.
static uint16_t table_index;

/// True once the module has been written, so the pass is a verification.
static bool verifying;

/// Parameters found to differ from the configuration during the audit.
static uint16_t mismatch_count;

/// Parameters the module would not report.
static uint16_t unreadable_count;

/// Parameters written in this run.
static uint16_t written_count;

/// True once the restore has already fallen back from R1 to RE.
static bool restore_fell_back;

/// The configuration.
static const xbee_prov_param_t *table;
static uint16_t table_count;

/// Tick by which the session must have closed.
static uint32_t close_deadline_tick;

/***************************************************************************//**
 * Report whether a deadline has been reached, tolerating tick counter wrap.
 ******************************************************************************/
static bool tick_reached(uint32_t deadline)
{
  return ((int32_t)(sl_sleeptimer_get_tick_count() - deadline) >= 0);
}

/***************************************************************************//**
 * Convert a millisecond interval into an absolute tick deadline from now.
 ******************************************************************************/
static uint32_t deadline_from_ms(uint32_t ms)
{
  uint32_t ticks = 0U;

  if (sl_sleeptimer_ms32_to_tick(ms, &ticks) != SL_STATUS_OK) {
    ticks = sl_sleeptimer_ms_to_tick((uint16_t)UINT16_MAX);
  }

  return sl_sleeptimer_get_tick_count() + ticks;
}

/***************************************************************************//**
 * Two command characters as text, for the log.
 *
 * The caller provides the buffer rather than this returning a shared one,
 * because two command names could otherwise appear in one call and collide.
 *
 * @param[out] text    Destination, at least CMD_TEXT_CAP characters.
 * @param[in]  command Packed command characters.
 *
 * @return @p text, or a literal when the identifier cannot be written out.
 ******************************************************************************/
static const char *command_name(char *text, uint16_t command)
{
  if (xbee_at_id_to_str(command, text, CMD_TEXT_CAP) != SL_STATUS_OK) {
    return "?";
  }

  return text;
}

/***************************************************************************//**
 * A parameter value as text for the log, masked when it is a credential.
 *
 * @param[out] text    Destination, at least VALUE_TEXT_CAP characters.
 * @param[in]  command Packed command characters.
 * @param[in]  value   Value bytes, may be NULL when len is 0.
 * @param[in]  len     Value length.
 *
 * @return @p text, or a literal when there is nothing to print or the value
 *         must not be logged.
 ******************************************************************************/
static const char *value_text(char *text,
                              uint16_t command,
                              const uint8_t *value,
                              uint16_t len)
{
  if (xbee_dump_is_secret(command)) {
    // The key, the Secure Session material and the Bluetooth SRP material never
    // reach the log. Only their width does, which says whether one is set.
    int written = snprintf(text, VALUE_TEXT_CAP, "(%u bytes, not logged)",
                           (unsigned)len);

    if ((written <= 0) || (written >= (int)VALUE_TEXT_CAP)) {
      return "(not logged)";
    }
    return text;
  }

  return byte_util_hex_text(text, VALUE_TEXT_CAP, value, len,
                            LOG_VALUE_MAX_BYTES);
}

/***************************************************************************//**
 * Stop with a reason.
 ******************************************************************************/
static void finish(xbee_prov_result_t outcome)
{
  result = outcome;
  state = PROV_DONE;
  req_active = false;

  if (xbee_provision_passed()) {
    APP_LOG_INFO("provisioning passed: %s", xbee_provision_result_str(outcome));
  } else {
    APP_LOG_ERROR("provisioning failed: %s", xbee_provision_result_str(outcome));
  }
}

/***************************************************************************//**
 * Close the session, then either stop or carry on to the reset.
 *
 * Every outcome reached with a session open goes through here, so the module is
 * never left in Command mode waiting out its own timeout. Closing is also the
 * step that applies whatever the session staged (manual lines 3101 to 3110),
 * which is why the successful path uses it too.
 *
 * @param[in] outcome The result to record once the session has closed, or
 *                    XBEE_PROV_RESULT_RUNNING to reset the module and verify
 *                    instead of stopping.
 ******************************************************************************/
static void close_session(xbee_prov_result_t outcome)
{
  pending_result = outcome;
  req_active = false;
  close_deadline_tick = deadline_from_ms(XBEE_PROV_CLOSE_TIMEOUT_MS);

  if (!xbee_cmd_session_is_open()
      || (xbee_cmd_session_close() != SL_STATUS_OK)) {
    // There is nothing to close, or the close could not be started. Either way
    // the module leaves Command mode on its own after its timeout.
    if (outcome == XBEE_PROV_RESULT_RUNNING) {
      state = PROV_CLOSE;
      return;
    }
    finish(outcome);
    return;
  }

  state = PROV_CLOSE;
}

/***************************************************************************//**
 * Ask the module to write its configuration to flash.
 *
 * The one flash write of a provisioning run. Nothing may be sent to the module
 * until it answers (manual lines 7093 to 7095), which the facade's one request
 * at a time already guarantees.
 ******************************************************************************/
static void start_commit(void)
{
  sl_status_t status = xbee_at_exec_timeout(XBEE_AT_WR, &req,
                                            XBEE_PROV_FLASH_TIMEOUT_MS);

  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("could not send the write command, status 0x%04X",
                  (unsigned)status);
    close_session(XBEE_PROV_RESULT_FAIL_COMMIT);
    return;
  }

  state = PROV_COMMIT;
  req_active = true;
}

/***************************************************************************//**
 * Report whether a command's value can be read back from the module.
 ******************************************************************************/
static bool is_readable(uint16_t command)
{
  return (xbee_at_table_validate_get(command) == SL_STATUS_OK);
}

/***************************************************************************//**
 * Start reading the next readable parameter from @p from onwards.
 *
 * @return false only when the table ran out with nothing left to read, which is
 *         the caller's signal that the pass is complete. True means the caller
 *         has nothing more to do: either a read is now in flight, or the run has
 *         already been stopped because one could not be started.
 ******************************************************************************/
static bool start_next_read(uint16_t from)
{
  uint16_t i;

  for (i = from; i < table_count; i++) {
    char name[CMD_TEXT_CAP];
    sl_status_t status;

    if (!is_readable(table[i].command)) {
      // The AES key is the one parameter the module never reports
      // (manual lines 5449 to 5458).
      continue;
    }

    status = xbee_at_get(table[i].command, &req);
    if (status != SL_STATUS_OK) {
      APP_LOG_ERROR("could not ask for %s, status 0x%04X",
                    command_name(name, table[i].command), (unsigned)status);
      close_session(verifying ? XBEE_PROV_RESULT_FAIL_VERIFY
                                   : XBEE_PROV_RESULT_FAIL_AUDIT);
      return true;
    }

    table_index = i;
    req_active = true;
    return true;
  }

  return false;
}

/***************************************************************************//**
 * Start writing the next parameter that deviates from the factory default.
 *
 * @return false only when the table ran out with nothing left to write, which is
 *         the caller's signal to commit. True means the caller has nothing more
 *         to do: either a write is now in flight, or the run has already been
 *         stopped because one could not be started.
 ******************************************************************************/
static bool start_next_write(uint16_t from)
{
  uint16_t i;

  for (i = from; i < table_count; i++) {
    const xbee_at_entry_t *entry = xbee_at_table_find(table[i].command);
    char name[CMD_TEXT_CAP];
    char text[VALUE_TEXT_CAP];
    sl_status_t status;

    // The module has just been restored, so anything left at its default is
    // already correct and writing it would only cost time.
    if (xbee_at_table_is_default(entry, table[i].value, table[i].len)) {
      continue;
    }

    status = xbee_at_set(table[i].command, table[i].value, table[i].len, &req);
    if (status != SL_STATUS_OK) {
      APP_LOG_ERROR("could not send %s, status 0x%04X",
                    command_name(name, table[i].command), (unsigned)status);
      close_session(XBEE_PROV_RESULT_FAIL_WRITE);
      return true;
    }

    APP_LOG_INFO("writing %s = %s",
                 command_name(name, table[i].command),
                 value_text(text, table[i].command,
                            table[i].value, table[i].len));
    table_index = i;
    req_active = true;
    return true;
  }

  return false;
}

/***************************************************************************//**
 * Begin the restore, or the fallback if the module refused the first choice.
 ******************************************************************************/
static void start_restore(bool fallback)
{
  char name[CMD_TEXT_CAP];
  uint16_t command;
  sl_status_t status;

#if XBEE_PROV_RESTORE == XBEE_PROV_RESTORE_R1
  // R1 ignores any custom defaults the module carries (manual lines 7131 to
  // 7140), which is what a production fixture wants. Not every firmware build
  // accepts it, so RE stands in when it is refused.
  command = fallback ? XBEE_AT_RE : XBEE_AT_R1;
#else
  // RE restores the custom defaults where any have been set
  // (manual lines 7090 to 7099).
  (void)fallback;
  command = XBEE_AT_RE;
#endif

  status = xbee_at_exec_timeout(command, &req, XBEE_PROV_FLASH_TIMEOUT_MS);
  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("could not send %s, status 0x%04X",
                  command_name(name, command), (unsigned)status);
    close_session(XBEE_PROV_RESULT_FAIL_RESTORE);
    return;
  }

  APP_LOG_INFO("restoring defaults with %s", command_name(name, command));
  state = PROV_RESTORE;
  req_active = true;
}

/***************************************************************************//**
 * Begin the audit, or the verification pass after the reset.
 ******************************************************************************/
static void start_read_pass(void)
{
  mismatch_count = 0U;
  unreadable_count = 0U;

  APP_LOG_INFO("%s the module's configuration, %u parameters",
               verifying ? "verifying" : "reading",
               (unsigned)table_count);

  state = PROV_AUDIT;
  if (!start_next_read(0U)) {
    // Nothing in the table can be read at all, which cannot happen with a
    // table the host tests accept.
    close_session(XBEE_PROV_RESULT_FAIL_AUDIT);
  }
}

/***************************************************************************//**
 * Handle one completed read during the audit or the verification pass.
 ******************************************************************************/
static void handle_read_result(void)
{
  const xbee_at_entry_t *entry = xbee_at_table_find(table[table_index].command);
  const uint16_t command = table[table_index].command;
  char cmd[CMD_TEXT_CAP];
  const char *name = command_name(cmd, command);
  char actual[VALUE_TEXT_CAP];
  char wanted[VALUE_TEXT_CAP];

  if (req.result != SL_STATUS_OK) {
    // A parameter this variant does not carry is not a provisioning failure:
    // the SPI pin commands, for instance, apply to the surface mount part only.
    APP_LOG_WARNING("could not read %s, status 0x%04X", name,
                    (unsigned)req.result);
    unreadable_count++;
  } else if (!xbee_at_table_values_equal(entry, req.value, req.value_len,
                                         table[table_index].value, table[table_index].len)) {
    mismatch_count++;
    if (verifying) {
      APP_LOG_ERROR("%s reads back as %s, expected %s", name,
                    value_text(actual, command, req.value, req.value_len),
                    value_text(wanted, command, table[table_index].value,
                               table[table_index].len));
      close_session(XBEE_PROV_RESULT_FAIL_VERIFY);
      return;
    }
    APP_LOG_INFO("%s is %s, configured as %s", name,
                 value_text(actual, command, req.value, req.value_len),
                 value_text(wanted, command, table[table_index].value,
                            table[table_index].len));
  } else {
    // Matches.
  }

  if (start_next_read((uint16_t)(table_index + 1U))) {
    return;
  }

  // The pass is complete.
  if (unreadable_count > 0U) {
    APP_LOG_WARNING("%u parameters could not be read and were not checked",
                    (unsigned)unreadable_count);
  }

  if (verifying) {
    APP_LOG_INFO("verified: every readable parameter matches");
    close_session(XBEE_PROV_RESULT_WRITTEN);
    return;
  }

  if (mismatch_count == 0U) {
    APP_LOG_INFO("module already matches the configuration, nothing to write");
    close_session(XBEE_PROV_RESULT_ALREADY_PROVISIONED);
    return;
  }

  APP_LOG_INFO("%u parameters differ, provisioning the module",
               (unsigned)mismatch_count);
  start_restore(false);
}

/***************************************************************************//**
 * Advance the sequence while a request is outstanding.
 ******************************************************************************/
static void process_request(void)
{
  if (!xbee_at_req_complete(&req)) {
    return;
  }
  req_active = false;

  switch (state) {
    case PROV_AUDIT:
      handle_read_result();
      break;

    case PROV_RESTORE:
      if (req.result != SL_STATUS_OK) {
#if XBEE_PROV_RESTORE == XBEE_PROV_RESTORE_R1
        if (!restore_fell_back) {
          // The module does not accept the factory restore. Fall back to the
          // ordinary one, which honours any custom defaults it carries.
          APP_LOG_WARNING("module refused R1, status 0x%04X: falling back to RE",
                          (unsigned)req.result);
          restore_fell_back = true;
          start_restore(true);
          return;
        }
#endif
        APP_LOG_ERROR("restore failed, status 0x%04X", (unsigned)req.result);
        close_session(XBEE_PROV_RESULT_FAIL_RESTORE);
        return;
      }

      written_count = 0U;
      state = PROV_WRITE;
      if (start_next_write(0U)) {
        written_count++;
      } else {
        // Every configured value is a factory default, so the restore alone has
        // done the job. The commit still runs, to persist the restored state.
        APP_LOG_INFO("no parameter deviates from the default");
        start_commit();
      }
      break;

    case PROV_WRITE:
      if (req.result != SL_STATUS_OK) {
        char name[CMD_TEXT_CAP];

        APP_LOG_ERROR("module refused %s, status 0x%04X",
                      command_name(name, table[table_index].command),
                      (unsigned)req.result);
        close_session(XBEE_PROV_RESULT_FAIL_WRITE);
        return;
      }
      if (start_next_write((uint16_t)(table_index + 1U))) {
        written_count++;
        return;
      }
      APP_LOG_INFO("%u parameters written, committing to flash",
                   (unsigned)written_count);
      start_commit();
      break;

    case PROV_COMMIT:
      if (req.result != SL_STATUS_OK) {
        APP_LOG_ERROR("write to flash failed, status 0x%04X",
                      (unsigned)req.result);
        close_session(XBEE_PROV_RESULT_FAIL_COMMIT);
        return;
      }
      APP_LOG_INFO("configuration committed, resetting the module");
      // Leaving Command mode applies the staged values, and the reset then
      // brings the module up on them.
      close_session(XBEE_PROV_RESULT_RUNNING);
      break;

    default:
      break;
  }
}

/***************************************************************************//**
 * Log the phase marker for a module that answered bring-up.
 *
 * The parameters bring-up read are not restated here: the dump that follows
 * prints every value the module will report, so a line naming a few of them
 * would only repeat it.
 ******************************************************************************/
static void report_module(void)
{
  xbee_info_t module;

  if (xbee_get_info(&module) != SL_STATUS_OK) {
    return;
  }

  APP_LOG_INFO("module ready in %s mode",
               (xbee_get_mode() == XBEE_MODE_TRANSPARENT) ? "transparent"
                 : ((xbee_get_mode() == XBEE_MODE_API1) ? "API 1" : "API 2"));
}

/***************************************************************************//**
 * Start provisioning.
 ******************************************************************************/
sl_status_t xbee_provision_init(void)
{
  static const xbee_config_t config = {
    .mode = XBEE_MODE_AUTO,
    .power_cycle = (XBEE_PROV_POWER_CYCLE != 0),
    .settle_ms = 0U,         // use the configured default
    .probe_timeout_ms = 0U,  // use the configured default
  };
  sl_status_t status;

  table = xbee_provision_table_get(&table_count);
  if ((table == NULL) || (table_count == 0U)) {
    APP_LOG_ERROR("the configuration table is empty");
    finish(XBEE_PROV_RESULT_FAIL_BRINGUP);
    return SL_STATUS_INVALID_STATE;
  }

  result = XBEE_PROV_RESULT_RUNNING;
  pending_result = XBEE_PROV_RESULT_RUNNING;
  req_active = false;
  verifying = false;
  restore_fell_back = false;
  written_count = 0U;
  table_index = 0U;

  APP_LOG_INFO("provisioning %u parameters from the configuration header",
               (unsigned)table_count);

  status = xbee_init(&config);
  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("could not start, status 0x%04X", (unsigned)status);
    finish(XBEE_PROV_RESULT_FAIL_BRINGUP);
    return status;
  }

  state = PROV_BRINGUP;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Advance the sequence.
 ******************************************************************************/
void xbee_provision_process(void)
{
  if (state == PROV_IDLE) {
    return;
  }

  (void)xbee_process();

  if (req_active) {
    process_request();
    return;
  }

  switch (state) {
    case PROV_BRINGUP:
      if (xbee_get_state() == XBEE_STATE_FAILED) {
        APP_LOG_ERROR("module did not answer, status 0x%04X",
                      (unsigned)xbee_get_result());
        finish(XBEE_PROV_RESULT_FAIL_BRINGUP);
        return;
      }
      if (!xbee_is_ready()) {
        return;
      }

      report_module();

      if (verifying) {
        // The reset has happened and the module answered; check it came back in
        // the mode the configuration asked for before reading anything.
        xbee_mode_t expected = (XBEE_PROV_AP == 0U) ? XBEE_MODE_TRANSPARENT
                               : ((XBEE_PROV_AP == 1U) ? XBEE_MODE_API1
                                                       : XBEE_MODE_API2);

        if (xbee_get_mode() != expected) {
          APP_LOG_ERROR("module came back in the wrong mode after the reset");
          finish(XBEE_PROV_RESULT_FAIL_RESET);
          return;
        }
      }

      if (xbee_cmd_session_open() != SL_STATUS_OK) {
        APP_LOG_ERROR("could not start a Command mode session");
        finish(XBEE_PROV_RESULT_FAIL_SESSION);
        return;
      }
      state = PROV_OPEN;
      break;

    case PROV_OPEN: {
      sl_status_t status = xbee_cmd_session_status();

      if (status == SL_STATUS_IN_PROGRESS) {
        return;
      }
      if (status != SL_STATUS_OK) {
        APP_LOG_ERROR("Command mode session did not open, status 0x%04X",
                      (unsigned)status);
        finish(XBEE_PROV_RESULT_FAIL_SESSION);
        return;
      }

      APP_LOG_INFO("Command mode session open");

#if XBEE_PROV_DUMP
      // The verification pass skips the dump: the module has just been written
      // and the audit that follows checks every value against the header.
      if (!verifying && (xbee_dump_start() == SL_STATUS_OK)) {
        state = PROV_DUMP;
        break;
      }
#endif

      start_read_pass();
      break;
    }

    case PROV_DUMP:
      // The dump owns its own request, so req_active stays false and the
      // provisioning request is untouched throughout this step.
      xbee_dump_process();
      if (!xbee_dump_is_finished()) {
        return;
      }
      // A dump that could not finish is a diagnostic, never a provisioning
      // failure, so the outcome is deliberately not consulted here.
      start_read_pass();
      break;

    case PROV_CLOSE:
      if (xbee_cmd_session_is_open()) {
        if (!tick_reached(close_deadline_tick)) {
          return;
        }
        // The exit command went unanswered and the module's own timeout has not
        // closed the session either. Carrying on would talk into a session that
        // may still be open, so the run stops here.
        APP_LOG_ERROR("Command mode session did not close");
        finish((pending_result == XBEE_PROV_RESULT_RUNNING)
               ? XBEE_PROV_RESULT_FAIL_COMMIT : pending_result);
        return;
      }
      if (pending_result != XBEE_PROV_RESULT_RUNNING) {
        finish(pending_result);
        return;
      }

      // The session closed after the commit, so the staged values are applied.
      // Reset the module to bring it up on them, then verify.
      if (xbee_hw_reset() != SL_STATUS_OK) {
        APP_LOG_ERROR("could not reset the module");
        finish(XBEE_PROV_RESULT_FAIL_RESET);
        return;
      }
      verifying = true;
      state = PROV_RESET;
      break;

    case PROV_RESET:
      // xbee_hw_reset() runs the whole bring-up sequence again, so waiting for
      // it is exactly what PROV_BRINGUP does.
      state = PROV_BRINGUP;
      break;

    case PROV_AUDIT:
    case PROV_RESTORE:
    case PROV_WRITE:
    case PROV_COMMIT:
    case PROV_DONE:
    default:
      break;
  }
}

/***************************************************************************//**
 * Report whether the sequence has finished.
 ******************************************************************************/
bool xbee_provision_is_finished(void)
{
  return (state == PROV_DONE);
}

/***************************************************************************//**
 * Report whether the module ended up configured as the header describes.
 ******************************************************************************/
bool xbee_provision_passed(void)
{
  return ((result == XBEE_PROV_RESULT_ALREADY_PROVISIONED)
          || (result == XBEE_PROV_RESULT_WRITTEN));
}

/***************************************************************************//**
 * How the run ended.
 ******************************************************************************/
xbee_prov_result_t xbee_provision_get_result(void)
{
  return result;
}

/***************************************************************************//**
 * Text for a result.
 ******************************************************************************/
const char *xbee_provision_result_str(xbee_prov_result_t outcome)
{
  const char *text;

  switch (outcome) {
    case XBEE_PROV_RESULT_RUNNING:
      text = "still running";
      break;
    case XBEE_PROV_RESULT_ALREADY_PROVISIONED:
      text = "already provisioned, nothing written";
      break;
    case XBEE_PROV_RESULT_WRITTEN:
      text = "written, committed and verified";
      break;
    case XBEE_PROV_RESULT_FAIL_BRINGUP:
      text = "the module did not answer";
      break;
    case XBEE_PROV_RESULT_FAIL_SESSION:
      text = "Command mode could not be opened";
      break;
    case XBEE_PROV_RESULT_FAIL_AUDIT:
      text = "the configuration could not be read";
      break;
    case XBEE_PROV_RESULT_FAIL_RESTORE:
      text = "the module refused to restore its defaults";
      break;
    case XBEE_PROV_RESULT_FAIL_WRITE:
      text = "the module refused a parameter";
      break;
    case XBEE_PROV_RESULT_FAIL_COMMIT:
      text = "the write to flash failed";
      break;
    case XBEE_PROV_RESULT_FAIL_RESET:
      text = "the module did not come back as configured";
      break;
    case XBEE_PROV_RESULT_FAIL_VERIFY:
      text = "a parameter did not read back as written";
      break;
    default:
      text = "unknown";
      break;
  }

  return text;
}
