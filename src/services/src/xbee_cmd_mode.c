/***************************************************************************//**
 * @file
 * @brief Command mode transport: the escape sequence, AT text and replies.
 ******************************************************************************/

#include <string.h>

#include "sl_sleeptimer.h"

#include "byte_util.h"
#include "xbee_uart.h"
#include "xbee_cmd_mode.h"

/// Carriage return, which terminates every command and every reply line
/// (docs/manuals/xbee_90002273_ref_manual.md, lines 3080 to 3099).
#define CR  '\r'

/// Line feed. The module does not send one, but it is skipped defensively so a
/// host or adapter that inserts one cannot corrupt the next line.
#define LF  '\n'

/// Times the command character is repeated to enter Command mode.
#define ESCAPE_REPEAT  3U

/// Default command character, the module's CC (manual lines 6140 to 6155).
#define DEFAULT_COMMAND_CHAR  '+'

/// Default guard time in milliseconds, the module's GT (manual lines 6165 to 6173).
#define DEFAULT_GUARD_MS  1000U

/// Default Command mode timeout in milliseconds, the module's CT
/// (manual lines 6157 to 6164).
#define DEFAULT_TIMEOUT_MS  10000U

/// Transport state.
static xbee_cmd_mode_state_t state = XBEE_CMD_STATE_IDLE;

/// Module parameters this transport must match.
static char command_char = DEFAULT_COMMAND_CHAR;
static uint32_t guard_ms = DEFAULT_GUARD_MS;
static uint32_t session_timeout_ms = DEFAULT_TIMEOUT_MS;

/// Tick at which the current state gives up.
static uint32_t state_deadline_tick;

/// Tick at which the module is expected to close the session on its own.
static uint32_t session_deadline_tick;

/// Command currently in flight, or NULL.
static xbee_cmd_request_t *active_request;

/// Reply line being assembled.
static char line_buf[XBEE_CMD_MODE_LINE_MAX + 1U];
static uint16_t line_len;

/// True once the line buffer has overflowed; the rest of the line is discarded.
static bool line_overflow;

/// Buffer for the command being written.
static char cmd_buf[XBEE_CMD_MODE_CMD_MAX];

/// Transparent mode received data callback.
static xbee_cmd_data_cb_t data_cb;
static void *data_user;

/***************************************************************************//**
 * Report whether a deadline has been reached, tolerating tick counter wrap.
 ******************************************************************************/
static bool tick_reached(uint32_t deadline)
{
  return ((int32_t)(sl_sleeptimer_get_tick_count() - deadline) >= 0);
}

/***************************************************************************//**
 * Convert a millisecond interval into ticks.
 ******************************************************************************/
static uint32_t ms_to_ticks(uint32_t ms)
{
  uint32_t ticks = 0U;

  if (sl_sleeptimer_ms32_to_tick(ms, &ticks) != SL_STATUS_OK) {
    ticks = sl_sleeptimer_ms_to_tick((uint16_t)UINT16_MAX);
  }

  return ticks;
}

/***************************************************************************//**
 * Convert a millisecond interval into an absolute tick deadline from now.
 ******************************************************************************/
static uint32_t deadline_from_ms(uint32_t ms)
{
  return sl_sleeptimer_get_tick_count() + ms_to_ticks(ms);
}

/***************************************************************************//**
 * Write bytes to the module.
 *
 * When the line last fell silent is tracked by the UART driver rather than
 * here, so that a frame sent by the API transport counts towards the guard time
 * as well. Measuring only this transport's own writes would let the escape
 * sequence follow an API frame too closely, and the module would ignore it.
 ******************************************************************************/
static sl_status_t write_bytes(const uint8_t *data, uint16_t len)
{
  return xbee_uart_write(data, len);
}

/***************************************************************************//**
 * Push the session timeout out, as any accepted command does.
 ******************************************************************************/
static void refresh_session_deadline(void)
{
  uint32_t window = session_timeout_ms;

  // Give up slightly before the module does, so a command is never sent into a
  // window that has just closed.
  if (window > XBEE_CMD_MODE_CT_MARGIN_MS) {
    window -= XBEE_CMD_MODE_CT_MARGIN_MS;
  }

  session_deadline_tick = deadline_from_ms(window);
}

/***************************************************************************//**
 * Complete the command in flight.
 ******************************************************************************/
static void finish_request(sl_status_t result, xbee_cmd_line_type_t reply)
{
  if (active_request == NULL) {
    return;
  }

  active_request->reply = reply;
  active_request->result = result;
  active_request->state = (result == SL_STATUS_OK) ? XBEE_CMD_REQ_DONE
                                                   : XBEE_CMD_REQ_FAILED;
  active_request = NULL;
}

/***************************************************************************//**
 * Discard whatever has been collected of the current line.
 ******************************************************************************/
static void reset_line(void)
{
  line_len = 0U;
  line_overflow = false;
  line_buf[0] = '\0';
}

/***************************************************************************//**
 * Classify one reply line.
 ******************************************************************************/
xbee_cmd_line_type_t xbee_cmd_mode_classify_line(const char *line, uint16_t len)
{
  uint16_t i;

  if ((line == NULL) || (len == 0U)) {
    return XBEE_CMD_LINE_EMPTY;
  }

  if ((len == 2U) && (line[0] == 'O') && (line[1] == 'K')) {
    return XBEE_CMD_LINE_OK;
  }

  // Checked before the File System pattern, because "ERROR" also starts with an
  // uppercase E.
  if ((len == 5U) && (strncmp(line, "ERROR", 5) == 0)) {
    return XBEE_CMD_LINE_ERROR;
  }

  // A File System error is an uppercase E, one or more uppercase letters or
  // digits, then a space and a description (manual lines 5820 to 5826).
  if (line[0] == 'E') {
    for (i = 1U; i < len; i++) {
      char c = line[i];

      if (((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9'))) {
        continue;
      }
      // At least one code character must precede the space.
      return ((c == ' ') && (i > 1U)) ? XBEE_CMD_LINE_FS_ERROR
                                      : XBEE_CMD_LINE_VALUE;
    }
  }

  return XBEE_CMD_LINE_VALUE;
}

/***************************************************************************//**
 * Handle one assembled reply line.
 ******************************************************************************/
static void handle_line(const char *line, uint16_t len)
{
  xbee_cmd_line_type_t type = xbee_cmd_mode_classify_line(line, len);

  if (state == XBEE_CMD_STATE_WAIT_OK) {
    if (type == XBEE_CMD_LINE_OK) {
      state = XBEE_CMD_STATE_ACTIVE;
      refresh_session_deadline();
    }
    // Anything else during entry is noise from before the session opened.
    return;
  }

  if (state == XBEE_CMD_STATE_EXITING) {
    if (type == XBEE_CMD_LINE_OK) {
      state = XBEE_CMD_STATE_IDLE;
      finish_request(SL_STATUS_OK, type);
    }
    return;
  }

  if ((state != XBEE_CMD_STATE_BUSY) || (active_request == NULL)) {
    return;
  }

  active_request->line_count++;
  refresh_session_deadline();

  if (type == XBEE_CMD_LINE_VALUE) {
    // Keep the most recent value line; a multi-line reply streams the rest
    // through the callback.
    (void)memcpy(active_request->value, line, len);
    active_request->value[len] = '\0';
    active_request->value_len = len;
  }

  if (active_request->on_line != NULL) {
    active_request->on_line(active_request, line, len, active_request->user);
  }

  if (active_request->multiline) {
    // An empty line closes a discovery: the manual calls the second carriage
    // return the sign that the discovery timeout expired (line 5022).
    if (type == XBEE_CMD_LINE_EMPTY) {
      state = XBEE_CMD_STATE_ACTIVE;
      finish_request(SL_STATUS_OK, type);
    }
    return;
  }

  switch (type) {
    case XBEE_CMD_LINE_OK:
      state = XBEE_CMD_STATE_ACTIVE;
      finish_request(SL_STATUS_OK, type);
      break;

    case XBEE_CMD_LINE_VALUE:
      state = XBEE_CMD_STATE_ACTIVE;
      finish_request(SL_STATUS_OK, type);
      break;

    case XBEE_CMD_LINE_ERROR:
      state = XBEE_CMD_STATE_ACTIVE;
      finish_request(SL_STATUS_FAIL, type);
      break;

    case XBEE_CMD_LINE_FS_ERROR:
      // The named code is left in the request's value for the caller to read.
      (void)memcpy(active_request->value, line, len);
      active_request->value[len] = '\0';
      active_request->value_len = len;
      state = XBEE_CMD_STATE_ACTIVE;
      finish_request(SL_STATUS_NOT_SUPPORTED, type);
      break;

    case XBEE_CMD_LINE_EMPTY:
    default:
      // A stray empty line before the real reply; keep waiting.
      active_request->line_count--;
      break;
  }
}

/***************************************************************************//**
 * Feed one received byte into the line assembler.
 ******************************************************************************/
static void feed_byte(uint8_t byte)
{
  if (byte == LF) {
    return;
  }

  if (byte == CR) {
    if (line_overflow) {
      // The line was too long to trust, so it is dropped rather than reported
      // as a truncated value.
      reset_line();
      return;
    }
    line_buf[line_len] = '\0';
    handle_line(line_buf, line_len);
    reset_line();
    return;
  }

  if (line_len >= XBEE_CMD_MODE_LINE_MAX) {
    line_overflow = true;
    return;
  }

  line_buf[line_len] = (char)byte;
  line_len++;
}

/***************************************************************************//**
 * Start the transport in the idle state.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_init(void)
{
  state = XBEE_CMD_STATE_IDLE;
  command_char = DEFAULT_COMMAND_CHAR;
  guard_ms = DEFAULT_GUARD_MS;
  session_timeout_ms = DEFAULT_TIMEOUT_MS;
  active_request = NULL;
  data_cb = NULL;
  data_user = NULL;
  reset_line();

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Adopt the module's own Command mode parameters.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_set_params(char cc, uint32_t gt_ms, uint32_t ct_ms)
{
  if ((gt_ms == 0U) || (ct_ms == 0U)) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (state != XBEE_CMD_STATE_IDLE) {
    // Changing the guard time under a running sequence would invalidate the
    // silence already measured.
    return SL_STATUS_INVALID_STATE;
  }

  command_char = cc;
  guard_ms = gt_ms;
  session_timeout_ms = ct_ms;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Begin opening a session.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_enter(void)
{
  if (state != XBEE_CMD_STATE_IDLE) {
    return SL_STATUS_INVALID_STATE;
  }

  reset_line();
  state = XBEE_CMD_STATE_GUARD_PRE;
  // Measured from when the line last carried a byte, whichever transport sent
  // it, plus a margin so the silence is safely longer than the module's
  // threshold rather than level with it.
  state_deadline_tick = xbee_uart_last_tx_tick()
                        + ms_to_ticks(guard_ms + XBEE_CMD_MODE_GUARD_MARGIN_MS);

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Build and write a command line.
 ******************************************************************************/
static sl_status_t write_command(uint16_t command,
                                 const uint8_t *value,
                                 uint16_t len,
                                 xbee_cmd_value_fmt_t fmt)
{
  uint16_t pos = 0U;
  sl_status_t status;

  cmd_buf[pos++] = 'A';
  cmd_buf[pos++] = 'T';
  cmd_buf[pos++] = (char)(uint8_t)(command >> 8);
  cmd_buf[pos++] = (char)(uint8_t)command;

  if ((len > 0U) && (fmt == XBEE_CMD_VALUE_HEX)) {
    status = byte_util_hex_encode(value, len, &cmd_buf[pos],
                                  (uint16_t)(XBEE_CMD_MODE_CMD_MAX - pos));
    if (status != SL_STATUS_OK) {
      return status;
    }
    pos = (uint16_t)(pos + (len * 2U));
  } else if ((len > 0U) && (fmt == XBEE_CMD_VALUE_TEXT)) {
    // One byte is kept for the carriage return.
    if (((uint32_t)pos + len + 1U) > XBEE_CMD_MODE_CMD_MAX) {
      return SL_STATUS_WOULD_OVERFLOW;
    }
    (void)memcpy(&cmd_buf[pos], value, len);
    pos = (uint16_t)(pos + len);
  } else {
    // No value: a read, or a command that only executes.
  }

  if (((uint32_t)pos + 1U) > XBEE_CMD_MODE_CMD_MAX) {
    return SL_STATUS_WOULD_OVERFLOW;
  }
  cmd_buf[pos++] = CR;

  return write_bytes((const uint8_t *)cmd_buf, pos);
}

/***************************************************************************//**
 * Send one AT command, with or without a multi-line reply.
 ******************************************************************************/
static sl_status_t send_command(uint16_t command,
                                const uint8_t *value,
                                uint16_t len,
                                xbee_cmd_value_fmt_t fmt,
                                xbee_cmd_request_t *request,
                                uint32_t timeout_ms,
                                bool multiline,
                                xbee_cmd_line_cb_t callback,
                                void *user)
{
  sl_status_t status;

  if (request == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if ((value == NULL) && (len > 0U)) {
    return SL_STATUS_NULL_POINTER;
  }
  if (state != XBEE_CMD_STATE_ACTIVE) {
    return SL_STATUS_INVALID_STATE;
  }

  status = write_command(command, value, len, fmt);
  if (status != SL_STATUS_OK) {
    return status;
  }

  (void)memset(request, 0, sizeof(*request));
  request->state = XBEE_CMD_REQ_PENDING;
  request->result = SL_STATUS_IN_PROGRESS;
  request->command = command;
  request->multiline = multiline;
  request->deadline_tick = deadline_from_ms(timeout_ms);
  request->on_line = callback;
  request->user = user;

  active_request = request;
  state = XBEE_CMD_STATE_BUSY;
  reset_line();

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Send one AT command and collect its reply.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_send(uint16_t command,
                               const uint8_t *value,
                               uint16_t len,
                               xbee_cmd_value_fmt_t fmt,
                               xbee_cmd_request_t *request,
                               uint32_t timeout_ms)
{
  return send_command(command, value, len, fmt, request, timeout_ms,
                      false, NULL, NULL);
}

/***************************************************************************//**
 * Send one AT command whose reply spans several lines.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_send_multi(uint16_t command,
                                     const uint8_t *value,
                                     uint16_t len,
                                     xbee_cmd_value_fmt_t fmt,
                                     xbee_cmd_request_t *request,
                                     uint32_t timeout_ms,
                                     xbee_cmd_line_cb_t callback,
                                     void *user)
{
  return send_command(command, value, len, fmt, request, timeout_ms,
                      true, callback, user);
}

/***************************************************************************//**
 * Close the session with the exit command.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_exit(xbee_cmd_request_t *request)
{
  sl_status_t status;

  if ((state != XBEE_CMD_STATE_ACTIVE) && (state != XBEE_CMD_STATE_BUSY)) {
    return SL_STATUS_INVALID_STATE;
  }

  // "CN", Exit Command mode (manual lines 6174 to 6179).
  status = write_command(0x434EU, NULL, 0U, XBEE_CMD_VALUE_NONE);
  if (status != SL_STATUS_OK) {
    return status;
  }

  if (active_request != NULL) {
    // A command still in flight is abandoned: the session is closing under it.
    finish_request(SL_STATUS_ABORT, XBEE_CMD_LINE_EMPTY);
  }

  if (request != NULL) {
    (void)memset(request, 0, sizeof(*request));
    request->state = XBEE_CMD_REQ_PENDING;
    request->result = SL_STATUS_IN_PROGRESS;
    request->command = 0x434EU;
    request->deadline_tick = deadline_from_ms(XBEE_CMD_MODE_REPLY_TIMEOUT_MS);
    active_request = request;
  }

  state = XBEE_CMD_STATE_EXITING;
  state_deadline_tick = deadline_from_ms(XBEE_CMD_MODE_REPLY_TIMEOUT_MS);
  reset_line();

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Abandon the session without telling the module.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_abandon(sl_status_t result)
{
  if (active_request != NULL) {
    finish_request(result, XBEE_CMD_LINE_EMPTY);
  }

  state = XBEE_CMD_STATE_IDLE;
  reset_line();

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Current transport state.
 ******************************************************************************/
xbee_cmd_mode_state_t xbee_cmd_mode_get_state(void)
{
  return state;
}

/***************************************************************************//**
 * Report whether a command may be sent.
 ******************************************************************************/
bool xbee_cmd_mode_is_ready(void)
{
  return (state == XBEE_CMD_STATE_ACTIVE);
}

/***************************************************************************//**
 * Report whether a command has finished.
 ******************************************************************************/
bool xbee_cmd_mode_request_complete(const xbee_cmd_request_t *request)
{
  if (request == NULL) {
    return false;
  }

  return ((request->state == XBEE_CMD_REQ_DONE)
          || (request->state == XBEE_CMD_REQ_FAILED));
}

/***************************************************************************//**
 * Register the Transparent mode data callback.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_set_data_callback(xbee_cmd_data_cb_t callback, void *user)
{
  data_cb = callback;
  data_user = user;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Send Transparent mode data.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_send_data(const uint8_t *data, uint16_t len)
{
  if (data == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (state != XBEE_CMD_STATE_IDLE) {
    // Sending now would either be swallowed as a command or break the guard
    // time the escape sequence needs.
    return SL_STATUS_INVALID_STATE;
  }
  if (len == 0U) {
    return SL_STATUS_OK;
  }

  return write_bytes(data, len);
}

/***************************************************************************//**
 * Advance the entry sequence.
 ******************************************************************************/
static void process_entry(void)
{
  uint8_t escape[ESCAPE_REPEAT];
  uint16_t i;

  if (!tick_reached(state_deadline_tick)) {
    return;
  }

  for (i = 0U; i < ESCAPE_REPEAT; i++) {
    escape[i] = (uint8_t)command_char;
  }

  if (write_bytes(escape, ESCAPE_REPEAT) != SL_STATUS_OK) {
    // The transmitter is busy. Try again on the next iteration; the extra
    // silence does no harm.
    return;
  }

  state = XBEE_CMD_STATE_WAIT_OK;
  // The module waits another guard time after the sequence before answering.
  state_deadline_tick =
    deadline_from_ms(guard_ms + XBEE_CMD_MODE_OK_MARGIN_MS);
  reset_line();
}

/***************************************************************************//**
 * Drive the transport.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_process(void)
{
  uint8_t chunk[XBEE_CMD_MODE_RX_CHUNK];
  uint16_t count = 0U;
  uint16_t i;
  sl_status_t status;

  status = xbee_uart_read(chunk, (uint16_t)sizeof(chunk), &count);
  if (status != SL_STATUS_OK) {
    return status;
  }

  if (state == XBEE_CMD_STATE_IDLE) {
    // No session: everything received is Transparent mode radio data.
    if ((count > 0U) && (data_cb != NULL)) {
      data_cb(chunk, count, data_user);
    }
  } else {
    for (i = 0U; i < count; i++) {
      feed_byte(chunk[i]);
    }
  }

  switch (state) {
    case XBEE_CMD_STATE_GUARD_PRE:
      process_entry();
      break;

    case XBEE_CMD_STATE_WAIT_OK:
      if (tick_reached(state_deadline_tick)) {
        // No OK: the module is not in Transparent mode, or the baud rate does
        // not match (manual lines 3064 to 3067).
        state = XBEE_CMD_STATE_IDLE;
        reset_line();
      }
      break;

    case XBEE_CMD_STATE_BUSY:
      if ((active_request != NULL) && tick_reached(active_request->deadline_tick)) {
        bool collected = (active_request->line_count > 0U);
        bool multi = active_request->multiline;

        state = XBEE_CMD_STATE_ACTIVE;
        // For a discovery the window closing is the normal end, provided
        // something answered.
        finish_request((multi && collected) ? SL_STATUS_OK : SL_STATUS_TIMEOUT,
                       XBEE_CMD_LINE_EMPTY);
      }
      break;

    case XBEE_CMD_STATE_EXITING:
      if (tick_reached(state_deadline_tick)) {
        // The module leaves Command mode on its own timeout anyway, so the
        // session is treated as closed.
        state = XBEE_CMD_STATE_IDLE;
        finish_request(SL_STATUS_TIMEOUT, XBEE_CMD_LINE_EMPTY);
        reset_line();
      }
      break;

    case XBEE_CMD_STATE_ACTIVE:
      if (tick_reached(session_deadline_tick)) {
        // The module's own Command mode timeout is about to lapse, so the
        // session is gone (manual lines 3116 to 3124).
        state = XBEE_CMD_STATE_IDLE;
        reset_line();
      }
      break;

    case XBEE_CMD_STATE_IDLE:
    default:
      break;
  }

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Check that a request carries a readable value.
 ******************************************************************************/
static sl_status_t check_value(const xbee_cmd_request_t *request)
{
  if (request == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if ((request->state != XBEE_CMD_REQ_DONE) || (request->value_len == 0U)) {
    return SL_STATUS_INVALID_STATE;
  }

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Read a completed command's value as a 32-bit integer.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_value_u32(const xbee_cmd_request_t *request,
                                    uint32_t *value)
{
  sl_status_t status = check_value(request);

  if (status != SL_STATUS_OK) {
    return status;
  }
  if (value == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  return byte_util_hex_to_u32(request->value, request->value_len, value);
}

/***************************************************************************//**
 * Read a completed command's value as a 64-bit integer.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_value_u64(const xbee_cmd_request_t *request,
                                    uint64_t *value)
{
  sl_status_t status = check_value(request);

  if (status != SL_STATUS_OK) {
    return status;
  }
  if (value == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  return byte_util_hex_to_u64(request->value, request->value_len, value);
}

/***************************************************************************//**
 * Read a completed command's value as raw bytes.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_value_bytes(const xbee_cmd_request_t *request,
                                      uint8_t *out,
                                      uint16_t cap,
                                      uint16_t *out_len)
{
  sl_status_t status = check_value(request);

  if (status != SL_STATUS_OK) {
    return status;
  }

  return byte_util_hex_decode(request->value, request->value_len,
                              out, cap, out_len);
}
