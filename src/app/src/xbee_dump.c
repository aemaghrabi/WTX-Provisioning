/***************************************************************************//**
 * @file
 * @brief Reads every readable AT parameter from the module and logs it.
 *
 * One request is in flight at a time, as the facade requires, and the walk is
 * driven from the super loop: a read is started, the answer is logged when it
 * arrives, and the index moves on.
 *
 * Nothing here writes to the module. There is no xbee_at_set, no xbee_at_exec
 * and no reset, and the commands that would act rather than answer are excluded
 * by xbee_dump_is_dumpable() before anything is sent.
 *
 * No Command mode session is opened either. xbee_at_get() works in whatever
 * mode bring-up found: on a Transparent mode module the facade opens and keeps
 * a session by itself, and on an API mode module a forced session would cost a
 * guard time of silence and buy nothing, because the one command in the table
 * that needs Command mode is not read. Note that on a Transparent mode module
 * the transport therefore does put the module into Command mode. That is not a
 * write, and the module leaves Command mode on its own CT timeout once the dump
 * stops asking.
 ******************************************************************************/

#define APP_LOG_TAG  "dump"

#include <stdbool.h>

#include "sl_sleeptimer.h"

#include "app_log.h"
#include "xbee.h"
#include "xbee_at_table.h"
#include "xbee_uart.h"
#include "xbee_dump.h"
#include "xbee_dump_config.h"
#include "xbee_dump_format.h"

/// Where the walk has got to.
typedef enum {
  DUMP_IDLE,     ///< Not started.
  DUMP_BRINGUP,  ///< Waiting for the facade to find and read the module.
  DUMP_READ,     ///< Walking the command table.
  DUMP_DONE,     ///< Finished, for whatever reason.
} dump_state_t;

/// Current step.
static dump_state_t state = DUMP_IDLE;

/// Outcome, once there is one.
static xbee_dump_result_t result = XBEE_DUMP_RESULT_RUNNING;

/// The one request in flight.
static xbee_at_req_t req;

/// True while a request is outstanding.
static bool req_active;

/// Next command table index to consider.
static uint16_t table_index;

/// Category of the last heading written, valid once heading_written is true.
static uint8_t last_category;

/// True once a category heading has been written.
static bool heading_written;

/// Parameters read and reported.
static uint16_t read_count;

/// Parameters the module would not report.
static uint16_t unreadable_count;

/// Commands deliberately not asked for because they would act.
static uint16_t skipped_count;

/// Commands that carry no readable value at all.
static uint16_t not_parameter_count;

/// Tick the dump started on, for the elapsed time in the footer.
static uint32_t start_tick;

/// True while a dispatch is being retried rather than given up on.
static bool stall_active;

/// Tick by which a stalled dispatch must have been accepted.
static uint32_t stall_deadline_tick;

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
 * Two command characters as text, for the log.
 *
 * The caller provides the buffer, so that a command and its name can appear in
 * one log statement without the two aliasing each other.
 *
 * @param[out] text Destination, at least 4 characters.
 *
 * @return @p text.
 ******************************************************************************/
static const char *command_text(uint16_t command, char *text)
{
  if (xbee_at_id_to_str(command, text, 4U) != SL_STATUS_OK) {
    text[0] = '?';
    text[1] = '\0';
  }

  return text;
}

/***************************************************************************//**
 * Why a readable-looking command is not asked for.
 ******************************************************************************/
static const char *skip_reason(const xbee_at_entry_t *entry)
{
  if ((entry->flags & XBEE_AT_FLAG_MULTI_RESPONSE) != 0U) {
    return "answers on several lines and occupies the radio";
  }
  if (entry->type == XBEE_AT_TYPE_SUBCOMMAND) {
    return "subcommand interpreter, it has no value of its own";
  }

  return "it acts when it is asked, rather than answering";
}

/***************************************************************************//**
 * Write the heading for a command's category, if it is not the current one.
 ******************************************************************************/
static void write_heading(const xbee_at_entry_t *entry)
{
  if (heading_written && (entry->category == last_category)) {
    return;
  }

  APP_LOG_INFO("--- %s ---", xbee_dump_category_name(entry->category));
  last_category = entry->category;
  heading_written = true;
}

/***************************************************************************//**
 * Log what bring-up found about the module.
 ******************************************************************************/
static void report_module(void)
{
  xbee_info_t info;

  if (xbee_get_info(&info) != SL_STATUS_OK) {
    APP_LOG_WARNING("ready, but bring-up read no parameters");
    return;
  }

  APP_LOG_INFO("module %08lX%08lX, firmware 0x%04X, hardware 0x%04X, in %s mode",
               (unsigned long)info.serial_high,
               (unsigned long)info.serial_low,
               (unsigned)info.vr,
               (unsigned)info.hv,
               mode_name(xbee_get_mode()));
  APP_LOG_INFO("max payload %u bytes, output options %u, GT %u ms, CC 0x%02X, "
               "CT %u ms",
               (unsigned)info.np,
               (unsigned)info.ao,
               (unsigned)info.gt,
               (unsigned)info.cc,
               (unsigned)(info.ct * 100U));
}

/***************************************************************************//**
 * Log the totals and the state of the serial link, then stop.
 ******************************************************************************/
static void report_totals(void)
{
  xbee_uart_stats_t uart;
  uint32_t elapsed_ms =
    sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count() - start_tick);

  APP_LOG_INFO("dump complete: %u read, %u not readable, %u not asked for, "
               "%u carry no value",
               (unsigned)read_count,
               (unsigned)unreadable_count,
               (unsigned)skipped_count,
               (unsigned)not_parameter_count);

  if (xbee_uart_get_stats(&uart) == SL_STATUS_OK) {
    APP_LOG_INFO("elapsed %lu ms, serial link: %lu received, %lu sent, "
                 "%lu overruns, %lu errors",
                 (unsigned long)elapsed_ms,
                 (unsigned long)uart.rx_bytes,
                 (unsigned long)uart.tx_bytes,
                 (unsigned long)uart.rx_overruns,
                 (unsigned long)uart.rx_errors);
    if (uart.rx_overruns > 0U) {
      // Bytes were lost somewhere in the walk, so a parameter reported above
      // may not be what the module actually holds.
      APP_LOG_WARNING("receive ring overran: this dump may be incomplete");
    }
  } else {
    APP_LOG_INFO("elapsed %lu ms", (unsigned long)elapsed_ms);
  }
}

/***************************************************************************//**
 * Stop with a reason.
 ******************************************************************************/
static void finish(xbee_dump_result_t outcome)
{
  result = outcome;
  state = DUMP_DONE;
  req_active = false;

  if (outcome == XBEE_DUMP_RESULT_COMPLETE) {
    report_totals();
  }
}

/***************************************************************************//**
 * Record a parameter the module would not report.
 *
 * Not a failure: the command set covers every variant of the module, so a
 * parameter this one does not carry, such as the surface mount SPI pins, simply
 * answers an error. The walk carries on either way.
 ******************************************************************************/
static void report_unreadable(const xbee_at_entry_t *entry, sl_status_t status)
{
  char command[4];

  APP_LOG_WARNING("%s (%s) not readable, status 0x%04X",
                  command_text(entry->id, command),
                  xbee_at_table_name(entry),
                  (unsigned)status);
  unreadable_count++;
}

/***************************************************************************//**
 * Start reading the next parameter from @p from onwards.
 *
 * Commands that carry no value are passed over silently and only counted; the
 * ones that would act if they were asked get a line saying so, because their
 * absence from the dump is the first thing a reader will wonder about.
 *
 * A dispatch that cannot be accepted yet is retried rather than abandoned: in
 * Transparent mode the transport may be re-entering Command mode underneath.
 * XBEE_DUMP_STALL_TIMEOUT_MS bounds that so one parameter cannot stall the walk.
 *
 * Note that no failure here stops the dump. The only way out of DUMP_READ is
 * the end of the table, which is what keeps one uncooperative parameter from
 * costing every parameter after it. This is deliberately unlike the
 * provisioning application, where a failed read has to stop the run.
 *
 * @return false only when the table ran out, which is the caller's signal that
 *         the walk is complete. True means a read is now in flight, or one is
 *         waiting to be retried on the next call.
 ******************************************************************************/
static bool start_next_read(uint16_t from)
{
  uint16_t i;

  for (i = from; i < xbee_at_table_count(); i++) {
    const xbee_at_entry_t *entry = xbee_at_table_at(i);
    sl_status_t status;
    char command[4];

    if (entry == NULL) {
      continue;
    }

    if (!xbee_dump_is_dumpable(entry)) {
      if (xbee_at_table_validate_get(entry->id) != SL_STATUS_OK) {
        // An executable command or the write-only key: there is no value to
        // ask for, so there is nothing to say about it.
        not_parameter_count++;
        continue;
      }

      write_heading(entry);
      APP_LOG_INFO("%s (%s) not read: %s",
                   command_text(entry->id, command),
                   xbee_at_table_name(entry),
                   skip_reason(entry));
      skipped_count++;
      continue;
    }

    write_heading(entry);

    status = xbee_at_get(entry->id, &req);
    if (status == SL_STATUS_OK) {
      table_index = i;
      req_active = true;
      stall_active = false;
      return true;
    }

    if ((status == SL_STATUS_NOT_READY) || (status == SL_STATUS_BUSY)) {
      if (!stall_active) {
        stall_active = true;
        stall_deadline_tick = deadline_from_ms(XBEE_DUMP_STALL_TIMEOUT_MS);
      }
      if (!tick_reached(stall_deadline_tick)) {
        // Stay on this command and try again on the next call. The heading has
        // already been written, and write_heading() will not repeat it.
        table_index = i;
        return true;
      }
      stall_active = false;
    }

    report_unreadable(entry, status);
  }

  return false;
}

/***************************************************************************//**
 * Handle one completed read.
 ******************************************************************************/
static void handle_read_result(void)
{
  const xbee_at_entry_t *entry = xbee_at_table_at(table_index);
  char command[4];
  char text[XBEE_DUMP_VALUE_TEXT_CAP];

  if (entry == NULL) {
    // Cannot happen: the index came from the walk.
    table_index++;
    return;
  }

  if (req.result != SL_STATUS_OK) {
    report_unreadable(entry, req.result);
  } else {
    APP_LOG_INFO("%s (%s) = %s",
                 command_text(entry->id, command),
                 xbee_at_table_name(entry),
                 xbee_dump_format_value(entry, req.value, req.value_len,
                                        text, sizeof(text)));
    read_count++;
  }

  table_index++;
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

  if (state == DUMP_READ) {
    handle_read_result();
  }
}

sl_status_t xbee_dump_init(void)
{
  static const xbee_config_t config = {
    .mode = XBEE_MODE_AUTO,
    .power_cycle = (XBEE_DUMP_POWER_CYCLE != 0),
    .settle_ms = 0U,         // use the configured default
    .probe_timeout_ms = 0U,  // use the configured default
  };
  sl_status_t status;
  uint16_t total = xbee_at_table_count();
  uint16_t readable = 0U;
  uint16_t i;

  result = XBEE_DUMP_RESULT_RUNNING;
  req_active = false;
  table_index = 0U;
  heading_written = false;
  last_category = 0U;
  read_count = 0U;
  unreadable_count = 0U;
  skipped_count = 0U;
  not_parameter_count = 0U;
  stall_active = false;
  start_tick = sl_sleeptimer_get_tick_count();

  for (i = 0U; i < total; i++) {
    if (xbee_dump_is_dumpable(xbee_at_table_at(i))) {
      readable++;
    }
  }

  // Both numbers, because the difference is what a reader would otherwise spend
  // time looking for: most of it is commands that execute rather than hold a
  // value, and so have nothing to report.
  APP_LOG_INFO("XBee AT parameter dump: %u commands in the table, %u readable",
               (unsigned)total, (unsigned)readable);

  status = xbee_init(&config);
  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("could not start, status 0x%04X", (unsigned)status);
    finish(XBEE_DUMP_RESULT_FAIL_BRINGUP);
    return status;
  }

  state = DUMP_BRINGUP;
  return SL_STATUS_OK;
}

void xbee_dump_process(void)
{
  if (state == DUMP_IDLE) {
    return;
  }

  // Keep the facade running even once the walk is over, so the receive path
  // stays drained: an API mode module still sends modem status frames, and a
  // ring left to fill would be reported as an overrun by whatever looks next.
  (void)xbee_process();

  if (req_active) {
    process_request();
    return;
  }

  switch (state) {
    case DUMP_BRINGUP:
      if (xbee_get_state() == XBEE_STATE_FAILED) {
        APP_LOG_ERROR("module did not answer, status 0x%04X",
                      (unsigned)xbee_get_result());
        finish(XBEE_DUMP_RESULT_FAIL_BRINGUP);
        return;
      }
      if (!xbee_is_ready()) {
        return;
      }

      report_module();
      state = DUMP_READ;
      break;

    case DUMP_READ:
      // Either the next parameter, or another attempt at one whose dispatch was
      // refused a moment ago.
      if (!start_next_read(table_index)) {
        finish(XBEE_DUMP_RESULT_COMPLETE);
      }
      break;

    case DUMP_DONE:
    default:
      break;
  }
}

bool xbee_dump_is_finished(void)
{
  return (state == DUMP_DONE);
}

xbee_dump_result_t xbee_dump_get_result(void)
{
  return result;
}
