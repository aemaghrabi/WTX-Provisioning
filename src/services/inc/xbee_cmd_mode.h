/***************************************************************************//**
 * @file
 * @brief Command mode transport: the escape sequence, AT text and replies.
 *
 * Reaches the module's parameters when it is in Transparent mode, where no API
 * frames are available. Entry, commands and exit all follow the manual's
 * "Modes" chapter (docs/manuals/xbee_90002273_ref_manual.md, lines 3030 to
 * 3124):
 *
 * - Entry needs a guard time of silence, the command character three times, and
 *   another guard time of silence. The module then answers "OK" followed by a
 *   carriage return.
 * - A command is "AT", the two command characters, an optional value and a
 *   carriage return. Omitting the value reads the parameter.
 * - A set answers "OK" or "ERROR"; a read answers with the value. File System
 *   commands answer with a named error instead of "ERROR".
 * - The session closes on "ATCN" or after the module's own Command mode
 *   timeout with no input.
 *
 * All waits are state machine transitions driven from the super loop, so no
 * call here blocks. The transport also carries Transparent mode data when no
 * session is open, which is how received radio data reaches the application in
 * that mode.
 *
 * @note Entry takes at least two guard times, two seconds with the defaults, so
 *       it is far slower than an API frame. The facade only uses it when the
 *       module is actually in Transparent mode.
 * @note The transport never transmits while a guard timer is running, because
 *       any byte sent to the module restarts the silence the escape sequence
 *       needs.
 ******************************************************************************/

#ifndef XBEE_CMD_MODE_H
#define XBEE_CMD_MODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sl_status.h"

#include "xbee_cmd_mode_config.h"

/// Transport state.
typedef enum {
  XBEE_CMD_STATE_IDLE,       ///< No session. Transparent data flows.
  XBEE_CMD_STATE_GUARD_PRE,  ///< Waiting out the silence before the escape sequence.
  XBEE_CMD_STATE_WAIT_OK,    ///< Escape sequence sent, waiting for the OK.
  XBEE_CMD_STATE_ACTIVE,     ///< Session open, ready for a command.
  XBEE_CMD_STATE_BUSY,       ///< Command sent, collecting the reply.
  XBEE_CMD_STATE_EXITING,    ///< Exit command sent, waiting for the OK.
} xbee_cmd_mode_state_t;

/// What a reply line turned out to be.
typedef enum {
  XBEE_CMD_LINE_EMPTY,     ///< Nothing between two carriage returns.
  XBEE_CMD_LINE_OK,        ///< The literal "OK".
  XBEE_CMD_LINE_ERROR,     ///< The literal "ERROR".
  XBEE_CMD_LINE_FS_ERROR,  ///< A File System error such as "ENOENT ...".
  XBEE_CMD_LINE_VALUE,     ///< Anything else: a parameter value or a data line.
} xbee_cmd_line_type_t;

/// How a command's value should be written.
typedef enum {
  XBEE_CMD_VALUE_NONE,  ///< No value: a read, or a command that just executes.
  XBEE_CMD_VALUE_HEX,   ///< Bytes written as uppercase hexadecimal, no prefix.
  XBEE_CMD_VALUE_TEXT,  ///< Characters written as they are, for string parameters.
} xbee_cmd_value_fmt_t;

/// Progress of one command.
typedef enum {
  XBEE_CMD_REQ_IDLE,     ///< Never sent.
  XBEE_CMD_REQ_PENDING,  ///< Sent, collecting the reply.
  XBEE_CMD_REQ_DONE,     ///< A reply arrived. result is SL_STATUS_OK.
  XBEE_CMD_REQ_FAILED,   ///< Ended without a usable reply. See result.
} xbee_cmd_req_state_t;

struct xbee_cmd_request;

/***************************************************************************//**
 * Called for each line of a multi-line reply, in super-loop context.
 *
 * @param[in] request The command being answered.
 * @param[in] line    The line, NUL terminated, without its carriage return.
 * @param[in] len     Line length.
 * @param[in] user    The value stored in the request.
 ******************************************************************************/
typedef void (*xbee_cmd_line_cb_t)(const struct xbee_cmd_request *request,
                                   const char *line,
                                   uint16_t len,
                                   void *user);

/***************************************************************************//**
 * Called with data received while no Command mode session is open.
 *
 * In Transparent mode everything the module receives over the air is written
 * straight to the serial port, with no framing and no sender address.
 *
 * @param[in] data Received bytes. Valid only for the call.
 * @param[in] len  Number of bytes.
 * @param[in] user The value passed to xbee_cmd_mode_set_data_callback().
 ******************************************************************************/
typedef void (*xbee_cmd_data_cb_t)(const uint8_t *data, uint16_t len, void *user);

/// One outstanding command. The caller owns it; treat the fields as read only.
typedef struct xbee_cmd_request {
  xbee_cmd_req_state_t state;   ///< Progress.
  sl_status_t result;           ///< Outcome once complete. SL_STATUS_OK, SL_STATUS_TIMEOUT, SL_STATUS_ABORT, SL_STATUS_FAIL for ERROR, or SL_STATUS_NOT_SUPPORTED for a File System error.
  uint16_t command;             ///< Packed command characters that were sent.
  xbee_cmd_line_type_t reply;   ///< What the final line turned out to be.
  bool multiline;               ///< Several lines are expected; the timeout or an empty line ends the reply.
  uint16_t line_count;          ///< Lines received.
  uint32_t deadline_tick;       ///< Sleeptimer tick at which the command expires.
  char value[XBEE_CMD_MODE_LINE_MAX + 1U];  ///< Most recent value line, NUL terminated.
  uint16_t value_len;           ///< Length of that line.
  xbee_cmd_line_cb_t on_line;   ///< Optional, called for every line.
  void *user;                   ///< Passed back to on_line.
} xbee_cmd_request_t;

/***************************************************************************//**
 * Start the transport in the idle state.
 *
 * Does not touch the UART: call xbee_uart_init() first. The module's defaults
 * are assumed until xbee_cmd_mode_set_params() is told otherwise.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_init(void);

/***************************************************************************//**
 * Adopt the module's own Command mode parameters.
 *
 * The facade calls this once it has read CC, GT and CT, so that a module
 * configured away from the defaults is still reachable.
 *
 * @param[in] command_char Command character, the module's CC.
 * @param[in] guard_ms     Guard time in milliseconds, the module's GT.
 * @param[in] timeout_ms   Command mode timeout in milliseconds, the module's CT.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_INVALID_PARAMETER if guard_ms or timeout_ms is 0,
 *         SL_STATUS_INVALID_STATE if a session is open or being opened.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_set_params(char command_char,
                                     uint32_t guard_ms,
                                     uint32_t timeout_ms);

/***************************************************************************//**
 * Begin opening a session.
 *
 * Returns immediately. Drive xbee_cmd_mode_process() until the state reaches
 * XBEE_CMD_STATE_ACTIVE or falls back to XBEE_CMD_STATE_IDLE.
 *
 * @return SL_STATUS_OK once the sequence has started,
 *         SL_STATUS_INVALID_STATE if a session is already open or opening.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_enter(void);

/***************************************************************************//**
 * Close the session with the exit command.
 *
 * Applies any queued parameter change, as leaving Command mode does
 * (manual lines 3100 to 3110).
 *
 * @param[in,out] request Request tracking the exit, or NULL to not track it.
 *
 * @return SL_STATUS_OK once the command is sent,
 *         SL_STATUS_INVALID_STATE if no session is open,
 *         or the transmission error.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_exit(xbee_cmd_request_t *request);

/***************************************************************************//**
 * Abandon the session without telling the module.
 *
 * Used when the module has been reset or powered down, so the transport does
 * not keep believing a session is open. The module's own timeout closes its
 * side.
 *
 * @param[in] result Result to record on a command still in flight.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_abandon(sl_status_t result);

/***************************************************************************//**
 * Current transport state.
 *
 * @return The state.
 ******************************************************************************/
xbee_cmd_mode_state_t xbee_cmd_mode_get_state(void);

/***************************************************************************//**
 * Report whether a session is open and idle, ready for a command.
 *
 * @return true when a command may be sent.
 ******************************************************************************/
bool xbee_cmd_mode_is_ready(void);

/***************************************************************************//**
 * Send one AT command and collect its reply.
 *
 * @param[in]     command    Packed command characters.
 * @param[in]     value      Value bytes or characters, NULL for a read.
 * @param[in]     len        Value length, 0 for a read.
 * @param[in]     fmt        How to write the value.
 * @param[in,out] request    Request tracking the reply. Must not be NULL.
 * @param[in]     timeout_ms How long to wait for it.
 *
 * @return SL_STATUS_OK once the command is sent,
 *         SL_STATUS_NULL_POINTER if request is NULL, or value is NULL with a
 *         non-zero length,
 *         SL_STATUS_INVALID_STATE if no session is open or one is already in
 *         flight,
 *         SL_STATUS_WOULD_OVERFLOW if the command does not fit the buffer,
 *         or the transmission error.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_send(uint16_t command,
                               const uint8_t *value,
                               uint16_t len,
                               xbee_cmd_value_fmt_t fmt,
                               xbee_cmd_request_t *request,
                               uint32_t timeout_ms);

/***************************************************************************//**
 * Send one AT command whose reply spans several lines.
 *
 * Node Discover, Active Scan, Energy Detect and Version Long answer this way.
 * Collection ends at an empty line, which a discovery uses to signal that its
 * timeout expired (manual line 5022), or at @p timeout_ms.
 *
 * @param[in]     command    Packed command characters.
 * @param[in]     value      Value bytes or characters, NULL for none.
 * @param[in]     len        Value length.
 * @param[in]     fmt        How to write the value.
 * @param[in,out] request    Request tracking the reply. Must not be NULL.
 * @param[in]     timeout_ms Collection window.
 * @param[in]     callback   Called for each line, may be NULL.
 * @param[in]     user       Passed back to the callback.
 *
 * @return As xbee_cmd_mode_send().
 ******************************************************************************/
sl_status_t xbee_cmd_mode_send_multi(uint16_t command,
                                     const uint8_t *value,
                                     uint16_t len,
                                     xbee_cmd_value_fmt_t fmt,
                                     xbee_cmd_request_t *request,
                                     uint32_t timeout_ms,
                                     xbee_cmd_line_cb_t callback,
                                     void *user);

/***************************************************************************//**
 * Drive the transport. Call once per super-loop iteration.
 *
 * Advances the entry sequence, assembles reply lines, expires a command whose
 * deadline has passed, and closes the session when the module's own timeout is
 * about to lapse. Returns quickly and never blocks.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_process(void);

/***************************************************************************//**
 * Report whether a command has finished, either way.
 *
 * @param[in] request Request to inspect.
 *
 * @return true when the command is done or failed.
 ******************************************************************************/
bool xbee_cmd_mode_request_complete(const xbee_cmd_request_t *request);

/***************************************************************************//**
 * Register the callback for Transparent mode received data.
 *
 * @param[in] callback Callback, or NULL to discard the data.
 * @param[in] user     Passed back to the callback.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_set_data_callback(xbee_cmd_data_cb_t callback, void *user);

/***************************************************************************//**
 * Send Transparent mode data.
 *
 * The module packetizes and transmits it according to RO and NP
 * (manual lines 3005 to 3020). The destination is whatever DH and DL hold.
 *
 * @param[in] data Bytes to send.
 * @param[in] len  Number of bytes.
 *
 * @return SL_STATUS_OK once queued,
 *         SL_STATUS_NULL_POINTER if data is NULL,
 *         SL_STATUS_INVALID_STATE if a Command mode session is open or opening,
 *         or the transmission error.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_send_data(const uint8_t *data, uint16_t len);

/***************************************************************************//**
 * Classify one reply line.
 *
 * A pure function, separated so it can be unit tested on the host.
 *
 * A File System command reports failure as a named code rather than the usual
 * "ERROR": an uppercase E, one or more uppercase letters or digits, a space and
 * a description (manual lines 5820 to 5826).
 *
 * @param[in] line Line contents, without the carriage return.
 * @param[in] len  Line length.
 *
 * @return What the line is.
 ******************************************************************************/
xbee_cmd_line_type_t xbee_cmd_mode_classify_line(const char *line, uint16_t len);

/***************************************************************************//**
 * Read a completed command's value as a 32-bit integer.
 *
 * The module prints parameter values as hexadecimal, with or without a leading
 * 0x (manual lines 3094 to 3096).
 *
 * @param[in]  request Completed request.
 * @param[out] value   Decoded value.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if either argument is NULL,
 *         SL_STATUS_INVALID_STATE if the request did not return a value,
 *         or the conversion error.
 ******************************************************************************/
sl_status_t xbee_cmd_mode_value_u32(const xbee_cmd_request_t *request,
                                    uint32_t *value);

/***************************************************************************//**
 * Read a completed command's value as a 64-bit integer.
 *
 * @param[in]  request Completed request.
 * @param[out] value   Decoded value.
 *
 * @return As xbee_cmd_mode_value_u32().
 ******************************************************************************/
sl_status_t xbee_cmd_mode_value_u64(const xbee_cmd_request_t *request,
                                    uint64_t *value);

/***************************************************************************//**
 * Read a completed command's value as raw bytes.
 *
 * @param[in]  request Completed request.
 * @param[out] out     Destination bytes.
 * @param[in]  cap     Capacity of out.
 * @param[out] out_len Number of bytes written.
 *
 * @return As xbee_cmd_mode_value_u32().
 ******************************************************************************/
sl_status_t xbee_cmd_mode_value_bytes(const xbee_cmd_request_t *request,
                                      uint8_t *out,
                                      uint16_t cap,
                                      uint16_t *out_len);

#endif  // XBEE_CMD_MODE_H
