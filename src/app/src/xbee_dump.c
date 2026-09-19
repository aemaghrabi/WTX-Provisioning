/***************************************************************************//**
 * @file
 * @brief Reads and logs every parameter the XBee module will report.
 ******************************************************************************/

#define APP_LOG_TAG  "dump"

#include <stdbool.h>
#include <stdint.h>

#include "sl_sleeptimer.h"

#include "app_log.h"
#include "byte_util.h"
#include "xbee.h"
#include "xbee_at_table.h"
#include "xbee_dump_config.h"
#include "xbee_dump.h"

/// Characters needed for a command name: two characters and the terminator.
#define CMD_TEXT_CAP  3U

/// Characters needed for an abbreviated value: two per byte, an ellipsis and
/// the terminator.
#define VALUE_TEXT_CAP  ((XBEE_DUMP_VALUE_MAX_BYTES * 2U) + 4U)

/// Where the dump has got to.
typedef enum {
  DUMP_IDLE,   ///< Not started.
  DUMP_OPEN,   ///< Waiting for a Command mode session opened here.
  DUMP_READ,   ///< Walking the command table.
  DUMP_CLOSE,  ///< Closing the session opened here.
  DUMP_DONE,   ///< Finished.
} dump_state_t;

/// Current step.
static dump_state_t state = DUMP_IDLE;

/// Outcome, once there is one.
static sl_status_t result = SL_STATUS_IN_PROGRESS;

/// The one request in flight.
static xbee_at_req_t req;

/// True while that request is outstanding.
static bool req_active;

/// Position in the command table.
static uint16_t index;

/// True when the session was opened here and must be closed here.
static bool owns_session;

/// Parameters the module reported.
static uint16_t read_count;

/// Parameters the module answered with a refusal.
static uint16_t refused_count;

/// Parameters the module did not answer at all.
static uint16_t unanswered_count;

/// Commands the dump did not ask for.
static uint16_t skipped_count;

/// Unanswered reads since the last answered one.
static uint16_t consecutive_timeouts;

/// True once a bound has expired and the dump is waiting to stop.
///
/// The dump never abandons a request that is still in flight: the facade holds
/// one request slot, and walking away from it would leave that slot taken and
/// the next caller's request rejected with SL_STATUS_BUSY. The flag lets the
/// in-flight read complete, which the facade itself bounds, before stopping.
static bool expired;

/// Tick by which the whole dump must have finished.
static uint32_t total_deadline_tick;

/// Tick by which the session must have opened or closed.
static uint32_t session_deadline_tick;

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
 * @param[out] text Destination, at least CMD_TEXT_CAP characters.
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
 * Report whether the dump asks the module for a command's value.
 *
 * @param[in] entry Table entry, never NULL here.
 *
 * @return true when the command holds a readable stored value.
 ******************************************************************************/
static bool dump_wanted(const xbee_at_entry_t *entry)
{
  if (xbee_at_table_validate_get(entry->id) != SL_STATUS_OK) {
    // Write only, KY, and the commands that only execute.
    return false;
  }
  if ((entry->flags & XBEE_AT_FLAG_MULTI_RESPONSE) != 0U) {
    // ND, ED, FS and VL answer with a stream ended by a timeout rather than
    // with one value, so reading them here would cost a timeout each.
    return false;
  }
  if (entry->type == (uint8_t)XBEE_AT_TYPE_SUBCOMMAND) {
    // PY takes a text subcommand, not a stored value.
    return false;
  }
  if (entry->id == XBEE_AT_DN) {
    // A discovery, not a stored value.
    return false;
  }

  return true;
}

/***************************************************************************//**
 * Number of commands the dump will ask the module for.
 ******************************************************************************/
static uint16_t wanted_count(void)
{
  uint16_t count = 0U;
  uint16_t total = xbee_at_table_count();
  uint16_t i;

  for (i = 0U; i < total; i++) {
    const xbee_at_entry_t *entry = xbee_at_table_at(i);

    if ((entry != NULL) && dump_wanted(entry)) {
      count++;
    }
  }

  return count;
}

/***************************************************************************//**
 * Record the outcome and stop, closing a session opened here first.
 ******************************************************************************/
static void finish(sl_status_t outcome)
{
  result = outcome;
  req_active = false;

  APP_LOG_INFO("dump complete: %u read, %u refused, %u unanswered, "
               "%u not readable",
               (unsigned)read_count,
               (unsigned)refused_count,
               (unsigned)unanswered_count,
               (unsigned)skipped_count);

  if (!owns_session) {
    state = DUMP_DONE;
    return;
  }

  session_deadline_tick = deadline_from_ms(XBEE_DUMP_CLOSE_TIMEOUT_MS);
  if (!xbee_cmd_session_is_open()
      || (xbee_cmd_session_close() != SL_STATUS_OK)) {
    // Nothing to close, or the close could not be started. Either way the
    // module leaves Command mode on its own once CT lapses.
    owns_session = false;
    state = DUMP_DONE;
    return;
  }

  state = DUMP_CLOSE;
}

/***************************************************************************//**
 * Stop because the absolute deadline has passed.
 ******************************************************************************/
static void give_up_on_time(void)
{
  APP_LOG_WARNING("dump gave up after %u ms",
                  (unsigned)XBEE_DUMP_TOTAL_TIMEOUT_MS);
  finish(SL_STATUS_TIMEOUT);
}

/***************************************************************************//**
 * Log the one line a completed read produces.
 ******************************************************************************/
static void log_read_result(const xbee_at_entry_t *entry)
{
  char name[CMD_TEXT_CAP];
  char text[VALUE_TEXT_CAP];
  const char *cmd = command_name(name, entry->id);
  const char *label = xbee_at_table_name(entry);

  if (req.result == SL_STATUS_TIMEOUT) {
    unanswered_count++;
    consecutive_timeouts++;
    APP_LOG_WARNING("%s (%s) = (no answer)", cmd, label);
    return;
  }

  consecutive_timeouts = 0U;

  if (req.result != SL_STATUS_OK) {
    // The expected answer for a command this variant does not implement. It
    // costs one round trip, not a timeout, so it is not a warning.
    refused_count++;
    APP_LOG_INFO("%s (%s) = (refused)", cmd, label);
    return;
  }

  read_count++;

  if (xbee_dump_is_secret(entry->id)) {
    APP_LOG_INFO("%s (%s) = (%u bytes, not logged)",
                 cmd, label, (unsigned)req.value_len);
    return;
  }

  APP_LOG_INFO("%s (%s) = %s", cmd, label,
               byte_util_hex_text(text, (uint16_t)sizeof(text),
                                  req.value, req.value_len,
                                  XBEE_DUMP_VALUE_MAX_BYTES));
}

/***************************************************************************//**
 * Walk forward to the next command to dump and ask for it.
 *
 * Commands the dump does not ask for are stepped over without any input or
 * output, so a run of them costs no extra time. The one exception is a
 * credential that cannot be read, which gets a line of its own so the log is
 * honest that the value exists; that line ends the call, keeping the promise of
 * at most one log line per tick.
 ******************************************************************************/
static void advance(void)
{
  uint16_t total = xbee_at_table_count();

  while (index < total) {
    const xbee_at_entry_t *entry = xbee_at_table_at(index);
    sl_status_t status;

    if (entry == NULL) {
      index++;
      continue;
    }

    if (!dump_wanted(entry)) {
      skipped_count++;
      index++;

      if (xbee_dump_is_secret(entry->id)) {
        char name[CMD_TEXT_CAP];

        // KY: the module answers a read with a status, never the key
        // (manual lines 5449 to 5458).
        APP_LOG_INFO("%s (%s) = (write only, %u bytes)",
                     command_name(name, entry->id),
                     xbee_at_table_name(entry),
                     (unsigned)entry->max_len);
        return;
      }
      continue;
    }

    status = xbee_at_get(entry->id, &req);
    if (status != SL_STATUS_OK) {
      char name[CMD_TEXT_CAP];

      APP_LOG_ERROR("could not ask for %s, status 0x%04X: dump abandoned",
                    command_name(name, entry->id), (unsigned)status);
      finish(status);
      return;
    }

    req_active = true;
    return;
  }

  finish(SL_STATUS_OK);
}

/***************************************************************************//**
 * Start the dump.
 ******************************************************************************/
sl_status_t xbee_dump_start(void)
{
  sl_status_t status;

  if ((state != DUMP_IDLE) && (state != DUMP_DONE)) {
    return SL_STATUS_BUSY;
  }
  if (!xbee_is_ready()) {
    return SL_STATUS_NOT_READY;
  }

  result = SL_STATUS_IN_PROGRESS;
  req_active = false;
  index = 0U;
  read_count = 0U;
  refused_count = 0U;
  unanswered_count = 0U;
  skipped_count = 0U;
  consecutive_timeouts = 0U;
  expired = false;
  owns_session = false;
  total_deadline_tick = deadline_from_ms(XBEE_DUMP_TOTAL_TIMEOUT_MS);

  APP_LOG_INFO("reading every parameter the module will report, "
               "%u of %u commands",
               (unsigned)wanted_count(), (unsigned)xbee_at_table_count());

  if (xbee_cmd_session_is_open()) {
    // Somebody else's session. Reads inside it cost one round trip each,
    // because an accepted command restarts the module's CT window rather than
    // ending the session. It is left open for its owner.
    state = DUMP_READ;
    advance();
    return SL_STATUS_OK;
  }

  if (xbee_get_mode() != XBEE_MODE_TRANSPARENT) {
    // An API mode reaches parameters with frames. Forcing a session would route
    // every read through Command mode instead, which is slower.
    state = DUMP_READ;
    advance();
    return SL_STATUS_OK;
  }

  status = xbee_cmd_session_open();
  if (status != SL_STATUS_OK) {
    APP_LOG_WARNING("could not start a Command mode session, status 0x%04X: "
                    "no dump", (unsigned)status);
    result = status;
    state = DUMP_DONE;
    return status;
  }

  owns_session = true;
  session_deadline_tick = deadline_from_ms(XBEE_DUMP_OPEN_TIMEOUT_MS);
  state = DUMP_OPEN;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Advance the dump.
 ******************************************************************************/
void xbee_dump_process(void)
{
  if ((state == DUMP_IDLE) || (state == DUMP_DONE)) {
    return;
  }

  // The absolute bound. It covers every step, including a session that opens
  // but never answers a read, so no run of slow answers can go unbounded. A
  // read already in flight is still allowed to complete, see "expired".
  if ((state != DUMP_CLOSE) && tick_reached(total_deadline_tick)) {
    expired = true;
  }

  switch (state) {
    case DUMP_OPEN: {
      sl_status_t status;

      if (expired) {
        give_up_on_time();
        return;
      }

      status = xbee_cmd_session_status();

      if (status == SL_STATUS_IN_PROGRESS) {
        if (tick_reached(session_deadline_tick)) {
          APP_LOG_WARNING("Command mode session did not open: no dump");
          finish(SL_STATUS_TIMEOUT);
        }
        return;
      }
      if (status != SL_STATUS_OK) {
        APP_LOG_WARNING("Command mode session did not open, status 0x%04X: "
                        "no dump", (unsigned)status);
        owns_session = false;
        finish(status);
        return;
      }

      state = DUMP_READ;
      advance();
      break;
    }

    case DUMP_READ:
      if (req_active) {
        const xbee_at_entry_t *entry;

        // The facade bounds this wait itself: an unanswered request completes
        // with SL_STATUS_TIMEOUT rather than staying in flight.
        if (!xbee_at_req_complete(&req)) {
          return;
        }
        req_active = false;

        entry = xbee_at_table_at(index);
        index++;
        if (entry != NULL) {
          log_read_result(entry);
        }

        // The link bound. Unanswered reads in a row mean the module has
        // stopped talking, and every remaining command would cost another
        // request timeout.
        if (consecutive_timeouts >= XBEE_DUMP_MAX_CONSECUTIVE_TIMEOUTS) {
          APP_LOG_WARNING("%u reads in a row went unanswered: dump abandoned",
                          (unsigned)consecutive_timeouts);
          finish(SL_STATUS_TIMEOUT);
          return;
        }
        if (expired) {
          give_up_on_time();
          return;
        }
        // One parameter line per call: the next tick issues the next read.
        return;
      }

      if (expired) {
        give_up_on_time();
        return;
      }

      advance();
      break;

    case DUMP_CLOSE:
      if (xbee_cmd_session_is_open()) {
        if (!tick_reached(session_deadline_tick)) {
          return;
        }
        // The exit went unanswered and the module's own timeout has not closed
        // the session either. Nothing more can be done about it from here.
        APP_LOG_WARNING("Command mode session did not close");
      }
      owns_session = false;
      state = DUMP_DONE;
      break;

    case DUMP_IDLE:
    case DUMP_DONE:
    default:
      break;
  }
}

/***************************************************************************//**
 * Report whether the dump has finished.
 ******************************************************************************/
bool xbee_dump_is_finished(void)
{
  return (state == DUMP_DONE);
}

/***************************************************************************//**
 * How the dump ended.
 ******************************************************************************/
sl_status_t xbee_dump_get_result(void)
{
  return result;
}

/***************************************************************************//**
 * Report whether a command holds a credential that must never be logged.
 ******************************************************************************/
bool xbee_dump_is_secret(uint16_t command)
{
  bool secret;

  switch (command) {
    // AES Encryption Key.
    case XBEE_AT_KY:
    // Secure Session salt and verifier.
    case XBEE_AT_STAR_S:
    case XBEE_AT_STAR_V:
    case XBEE_AT_STAR_W:
    case XBEE_AT_STAR_X:
    case XBEE_AT_STAR_Y:
    // Bluetooth SRP salt and verifier.
    case XBEE_AT_DOLLAR_S:
    case XBEE_AT_DOLLAR_V:
    case XBEE_AT_DOLLAR_W:
    case XBEE_AT_DOLLAR_X:
    case XBEE_AT_DOLLAR_Y:
      secret = true;
      break;

    default:
      secret = false;
      break;
  }

  return secret;
}
