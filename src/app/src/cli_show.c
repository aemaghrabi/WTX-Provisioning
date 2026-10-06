/***************************************************************************//**
 * @file
 * @brief The console's "show xbee all" walk over the AT command table.
 *
 * One request is in flight at a time, as the facade requires, and the walk is
 * driven from the super loop: a read is started, the answer is printed when it
 * arrives, and the index moves on. The console keeps reading the terminal
 * throughout, so Ctrl-C reaches @ref cli_show_all_abort mid-walk.
 *
 * Nothing here writes to the module. The commands that would act rather than
 * answer are excluded by xbee_dump_is_dumpable() before anything is sent.
 *
 * The walk is deliberately a second implementation rather than a reuse of the
 * one in xbee_dump.c. They differ where it matters: this one has to yield to
 * input between parameters, stop part way through on request, and return to a
 * prompt, where the dump's runs once at boot and then halts. Unifying the two
 * behind one resumable walker is worth doing, and is recorded as a follow-up in
 * docs/plan/2026-09-20-cli-console.md rather than done here, because
 * xbee_dump.c has been verified on hardware. What is shared is everything that
 * decides *what* to print: xbee_dump_is_dumpable(), xbee_dump_category_name()
 * and xbee_dump_format_value().
 ******************************************************************************/

#include <stdbool.h>
#include <stddef.h>

#include "sl_sleeptimer.h"

#include "console_uart.h"
#include "device_sn.h"
#include "nvm_store.h"
#include "xbee.h"
#include "xbee_at_table.h"
#include "xbee_dump_format.h"

#include "cli_config.h"
#include "cli_show.h"

/// True while a walk is running.
static bool running;

/// True once Ctrl-C has asked the walk to stop.
///
/// The walk does not stop on the spot. A read already in flight has handed the
/// transport a pointer to @ref req, and that pointer stays live until the reply
/// or the timeout arrives, so reusing the request object before then would let
/// a late completion write into a request the console had moved on from. The
/// walk therefore finishes the read it is on, at most one command timeout, and
/// stops before starting another.
static bool abort_requested;

/// The one request in flight.
static xbee_at_req_t req;

/// True while that request is outstanding.
static bool req_active;

/// Command table index being read, or considered next.
static uint16_t table_index;

/// Category of the last heading written, valid once heading_written is true.
static uint8_t last_category;

/// True once a category heading has been written.
static bool heading_written;

/// Parameters read and reported.
static uint16_t read_count;

/// Parameters the module would not report.
static uint16_t unreadable_count;

/// Tick the walk started on, for the elapsed time in the summary.
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
 * Render a command's two characters into caller-provided storage.
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
 * Name of a serial mode, for the report.
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
 * Write the heading for a command's category, if it is not the current one.
 ******************************************************************************/
static void write_heading(const xbee_at_entry_t *entry)
{
  if (heading_written && (entry->category == last_category)) {
    return;
  }

  (void)console_uart_printf("\n%s\n",
                            xbee_dump_category_name(entry->category));
  last_category = entry->category;
  heading_written = true;
}

/***************************************************************************//**
 * Print the totals and stop.
 ******************************************************************************/
static void report_totals(bool aborted)
{
  uint32_t elapsed_ms =
    sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count() - start_tick);

  (void)console_uart_printf("\n%s: %u read, %u not readable, %lu ms\n",
                            aborted ? "Interrupted" : "Complete",
                            (unsigned)read_count,
                            (unsigned)unreadable_count,
                            (unsigned long)elapsed_ms);
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

  (void)console_uart_printf("  %-2s (%s) not readable, status 0x%04X\n",
                            command_text(entry->id, command),
                            xbee_at_table_name(entry),
                            (unsigned)status);
  unreadable_count++;
}

/***************************************************************************//**
 * Start reading the next readable parameter from @p from onwards.
 *
 * A dispatch that cannot be accepted yet is retried rather than abandoned: in
 * Transparent mode the transport may be re-entering Command mode underneath.
 * CLI_REQUEST_TIMEOUT_MS bounds that so one parameter cannot stall the walk.
 *
 * No failure here stops the walk. The only way out is the end of the table,
 * which is what keeps one uncooperative parameter from costing every parameter
 * after it.
 *
 * @return false only when the table ran out.
 ******************************************************************************/
static bool start_next_read(uint16_t from)
{
  uint16_t i;

  for (i = from; i < xbee_at_table_count(); i++) {
    const xbee_at_entry_t *entry = xbee_at_table_at(i);
    sl_status_t status;

    if (entry == NULL) {
      continue;
    }

    // Commands that act when asked, and those with no value to report, are
    // simply left out: the console is not a diagnostic transcript, and naming
    // each one would bury the parameters the operator asked for.
    if (!xbee_dump_is_dumpable(entry)) {
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
        stall_deadline_tick = deadline_from_ms(CLI_REQUEST_TIMEOUT_MS);
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
 * Print one completed read.
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
    (void)console_uart_printf("  %-2s (%s) = %s\n",
                              command_text(entry->id, command),
                              xbee_at_table_name(entry),
                              xbee_dump_format_value(entry, req.value,
                                                     req.value_len,
                                                     text, sizeof(text)));
    read_count++;
  }

  table_index++;
}

sl_status_t cli_show_all_start(void)
{
  if (running) {
    return SL_STATUS_INVALID_STATE;
  }

  running = true;
  req_active = false;
  abort_requested = false;
  table_index = 0U;
  last_category = 0U;
  heading_written = false;
  read_count = 0U;
  unreadable_count = 0U;
  stall_active = false;
  start_tick = sl_sleeptimer_get_tick_count();

  return SL_STATUS_OK;
}

bool cli_show_all_process(void)
{
  if (!running) {
    return false;
  }

  if (req_active) {
    if (!xbee_at_req_complete(&req)) {
      return true;
    }
    req_active = false;

    // An answer that arrived after Ctrl-C is dropped rather than printed: the
    // operator has asked for the output to stop.
    if (!abort_requested) {
      handle_read_result();
    }
  }

  if (abort_requested) {
    report_totals(true);
    running = false;
    return false;
  }

  // Back to the super loop between parameters, so that a stalled dispatch is
  // retried through the same path as an ordinary step and the terminal keeps
  // being read.
  if (!start_next_read(table_index)) {
    report_totals(false);
    running = false;
    return false;
  }

  return true;
}

void cli_show_all_abort(void)
{
  if (running) {
    abort_requested = true;
  }
}

void cli_show_info(void)
{
  xbee_info_t info;

  if (xbee_get_info(&info) != SL_STATUS_OK) {
    (void)console_uart_puts("% Bring-up read no parameters from the module\n");
    return;
  }

  (void)console_uart_printf("Address      %08lX%08lX\n",
                            (unsigned long)info.serial_high,
                            (unsigned long)info.serial_low);
  (void)console_uart_printf("Firmware     0x%04X\n", (unsigned)info.vr);
  (void)console_uart_printf("Hardware     0x%04X\n", (unsigned)info.hv);
  (void)console_uart_printf("Serial mode  %s (AP %u)\n",
                            mode_name(xbee_get_mode()),
                            (unsigned)info.ap);
  (void)console_uart_printf("Max payload  %u bytes\n", (unsigned)info.np);
  (void)console_uart_printf("Command mode GT %u ms, CC 0x%02X, CT %u ms\n",
                            (unsigned)info.gt,
                            (unsigned)info.cc,
                            (unsigned)(info.ct * 100U));
}

void cli_show_device_sn(void)
{
  char stored[DEVICE_SN_STR_LEN];
  char shown[DEVICE_SN_STR_LEN];
  size_t stored_len = 0U;
  sl_status_t status = nvm_store_read_device_sn(stored, sizeof(stored),
                                                &stored_len);

  switch (status) {
    case SL_STATUS_OK:
      // Printed through the sanitised copy in every case: the stored bytes are
      // not guaranteed to be text, let alone NUL terminated.
      (void)device_sn_to_printable(stored, shown, sizeof(shown));
      status = device_sn_validate(stored);
      if (status == SL_STATUS_OK) {
        (void)console_uart_printf("Serial number  %s\n", shown);
      } else if (status == SL_STATUS_INVALID_SIGNATURE) {
        (void)console_uart_printf("Serial number  %s (check digit invalid)\n",
                                  shown);
      } else {
        (void)console_uart_printf("Serial number  %s (not in the form "
                                  "YYWW-NNNNN-C)\n", shown);
      }
      break;

    case SL_STATUS_NOT_FOUND:
      (void)console_uart_puts("Serial number  not set\n");
      break;

    case SL_STATUS_INVALID_COUNT:
      (void)console_uart_printf("Serial number  not set (the stored object is "
                                "%u bytes, not %u)\n",
                                (unsigned)stored_len,
                                (unsigned)DEVICE_SN_STR_LEN);
      break;

    case SL_STATUS_INVALID_TYPE:
      (void)console_uart_puts("Serial number  not set (a counter object is "
                              "stored under its key)\n");
      break;

    default:
      (void)console_uart_printf("%% Could not read MCU NVM, status 0x%04X\n",
                                (unsigned)status);
      break;
  }
}
