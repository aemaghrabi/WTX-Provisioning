/***************************************************************************//**
 * @file
 * @brief Mode-independent facade over the XBee module.
 *
 * The one interface the application uses. It hides which serial mode the module
 * is in: the same calls work whether the module is in API mode 1, API mode 2 or
 * Transparent mode, where parameters are reached through Command mode instead
 * of API frames.
 *
 * @code
 * static const xbee_config_t cfg = {
 *   .mode = XBEE_MODE_AUTO,
 *   .power_cycle = true,
 * };
 * static xbee_at_req_t req;
 *
 * xbee_init(&cfg);
 *
 * // once per super-loop iteration
 * xbee_process();
 *
 * if (xbee_is_ready()) {
 *   xbee_at_get(XBEE_AT_SH, &req);
 * }
 * if (xbee_at_req_complete(&req) && (req.result == SL_STATUS_OK)) {
 *   uint32_t serial_high;
 *   xbee_at_req_value_u32(&req, &serial_high);
 * }
 * @endcode
 *
 * Bring-up runs as a state machine: apply power, wait for the module to boot,
 * work out which mode it is in, read the parameters that matter, then report
 * ready. Nothing blocks, so xbee_process() always returns quickly.
 *
 * @note One application request may be in flight at a time. A second call
 *       returns SL_STATUS_BUSY. Command mode is inherently serial, and
 *       provisioning issues one command at a time in any case.
 * @note Operations that only exist in API mode, such as remote commands and
 *       secure sessions, return SL_STATUS_NOT_SUPPORTED in Transparent mode.
 ******************************************************************************/

#ifndef XBEE_H
#define XBEE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sl_status.h"

#include "xbee_config.h"
#include "xbee_api.h"
#include "xbee_at_table.h"
#include "xbee_cmd_mode.h"
#include "xbee_frame.h"

/// Serial mode of the module.
typedef enum {
  XBEE_MODE_AUTO,         ///< Only as a request: detect which mode the module is in.
  XBEE_MODE_TRANSPARENT,  ///< AP=0. Parameters through Command mode.
  XBEE_MODE_API1,         ///< AP=1. API frames without escaping.
  XBEE_MODE_API2,         ///< AP=2. API frames with escaping.
} xbee_mode_t;

/// Progress of bring-up.
typedef enum {
  XBEE_STATE_OFF,            ///< Not started, or the module is unpowered.
  XBEE_STATE_RESETTING,      ///< Holding the module in reset.
  XBEE_STATE_SETTLING,       ///< Waiting for the module to boot.
  XBEE_STATE_PROBE_API,      ///< Asking for AP with an API frame.
  XBEE_STATE_PROBE_CMD,      ///< Opening a Command mode session.
  XBEE_STATE_PROBE_CMD_READ, ///< Asking for AP through Command mode.
  XBEE_STATE_READ_PARAMS,    ///< Reading the parameters bring-up needs.
  XBEE_STATE_READY,          ///< Usable.
  XBEE_STATE_ASLEEP,         ///< Put to sleep with the request line.
  XBEE_STATE_FAILED,         ///< Bring-up did not succeed. See xbee_get_result().
} xbee_state_t;

/// How the facade was asked to start.
typedef struct {
  xbee_mode_t mode;             ///< Requested mode, XBEE_MODE_AUTO to detect it.
  bool power_cycle;             ///< Remove and reapply the supply before probing.
  uint32_t settle_ms;           ///< Boot time to allow, 0 for XBEE_POWER_SETTLE_MS.
  uint32_t probe_timeout_ms;    ///< Per probe step, 0 for XBEE_PROBE_TIMEOUT_MS.
} xbee_config_t;

/// Parameters read during bring-up.
typedef struct {
  uint8_t  ap;        ///< API Enable, as the module reports it.
  uint8_t  ao;        ///< API Output Options. 2 by default, meaning legacy frames.
  uint16_t np;        ///< Maximum Packet Payload Bytes.
  uint16_t vr;        ///< Firmware Version.
  uint16_t hv;        ///< Hardware Version.
  uint32_t serial_high; ///< SH, the upper half of the 64-bit address.
  uint32_t serial_low;  ///< SL, the lower half.
  uint16_t gt;        ///< Guard Times, in milliseconds.
  uint8_t  cc;        ///< Command Character.
  uint16_t ct;        ///< Command Mode Timeout, in units of 100 ms.
  bool     valid;     ///< True once bring-up has filled these in.
} xbee_info_t;

/// Progress of one application request.
typedef enum {
  XBEE_REQ_IDLE,     ///< Never sent.
  XBEE_REQ_PENDING,  ///< In flight.
  XBEE_REQ_DONE,     ///< Completed. result is SL_STATUS_OK.
  XBEE_REQ_FAILED,   ///< Completed without a usable answer. See result.
} xbee_req_state_t;

/// What a request is asking the module to do.
typedef enum {
  XBEE_OP_GET,        ///< Read a parameter.
  XBEE_OP_SET,        ///< Write a parameter, applied at once.
  XBEE_OP_QUEUE_SET,  ///< Write a parameter, applied on the next apply.
  XBEE_OP_EXEC,       ///< Run a command that takes no parameter.
} xbee_op_t;

/// One application request. The caller owns it; treat the fields as read only.
typedef struct {
  xbee_req_state_t state;   ///< Progress.
  sl_status_t result;       ///< Outcome once complete.
  uint16_t command;         ///< Packed command characters.
  uint8_t  op;              ///< xbee_op_t.
  uint8_t  status;          ///< xbee_at_status_t reported by the module.
  uint8_t  value[XBEE_AT_VALUE_MAX];  ///< Value returned by a read.
  uint16_t value_len;       ///< Length of that value.

  /// @cond INTERNAL
  // Set up by the facade before the request is sent, and used while it is in
  // flight. Only one transport carries a given request, so they share storage.
  uint8_t  value_in[XBEE_AT_VALUE_MAX];
  uint16_t value_in_len;
  bool     awaiting_session;
  union {
    xbee_api_request_t api;
    xbee_cmd_request_t cmd;
  } t;
  /// @endcond
} xbee_at_req_t;

/***************************************************************************//**
 * Received data, however it arrived.
 *
 * Unifies the modern and legacy receive frames and Transparent mode bytes into
 * one shape. In Transparent mode neither address is known, so both are reported
 * as the unknown values.
 *
 * @param[in] addr64  Sender's 64-bit address, XBEE_ADDR64_UNKNOWN if not known.
 * @param[in] addr16  Sender's 16-bit address, XBEE_ADDR16_UNKNOWN if not known.
 * @param[in] options Receive options, 0 in Transparent mode.
 * @param[in] data    Payload. Valid only for the call.
 * @param[in] len     Payload length.
 * @param[in] user    The value passed to xbee_set_data_callback().
 ******************************************************************************/
typedef void (*xbee_data_cb_t)(uint64_t addr64,
                               uint16_t addr16,
                               uint8_t options,
                               const uint8_t *data,
                               uint16_t len,
                               void *user);

/***************************************************************************//**
 * Every unsolicited frame, decoded, for callers that want more than the data.
 *
 * @param[in] frame The frame. Valid only for the call.
 * @param[in] user  The value passed to xbee_set_frame_callback().
 ******************************************************************************/
typedef void (*xbee_frame_cb_t)(const xbee_frame_t *frame, void *user);

/***************************************************************************//**
 * A change in the module's status, such as a reset or an association change.
 *
 * @param[in] status The modem status code.
 * @param[in] user   The value passed to xbee_set_modem_status_callback().
 ******************************************************************************/
typedef void (*xbee_modem_status_cb_t)(xbee_modem_status_t status, void *user);

/***************************************************************************//**
 * Initialise the drivers and start bring-up.
 *
 * Returns at once. Drive xbee_process() until xbee_is_ready() or the state
 * reaches XBEE_STATE_FAILED.
 *
 * @param[in] config How to start, or NULL for the defaults: detect the mode and
 *                   power cycle the module.
 *
 * @return SL_STATUS_OK once bring-up has started, or the driver error that
 *         prevented it.
 ******************************************************************************/
sl_status_t xbee_init(const xbee_config_t *config);

/***************************************************************************//**
 * Shut down: abandon any request, stop the serial path and remove the supply.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_deinit(void);

/***************************************************************************//**
 * Drive the facade and the active transport. Call once per super-loop
 * iteration.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_process(void);

/***************************************************************************//**
 * Current bring-up state.
 *
 * @return The state.
 ******************************************************************************/
xbee_state_t xbee_get_state(void);

/***************************************************************************//**
 * Report whether the module is usable.
 *
 * @return true when a request may be issued.
 ******************************************************************************/
bool xbee_is_ready(void);

/***************************************************************************//**
 * Why bring-up failed.
 *
 * @return SL_STATUS_OK while bring-up is progressing or has succeeded,
 *         otherwise the reason it stopped.
 ******************************************************************************/
sl_status_t xbee_get_result(void);

/***************************************************************************//**
 * The mode the module was found to be in.
 *
 * @return The detected mode. XBEE_MODE_AUTO until it is known.
 ******************************************************************************/
xbee_mode_t xbee_get_mode(void);

/***************************************************************************//**
 * Parameters read during bring-up.
 *
 * @param[out] info Destination.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if info is NULL,
 *         SL_STATUS_NOT_READY if bring-up has not read them yet.
 ******************************************************************************/
sl_status_t xbee_get_info(xbee_info_t *info);

/***************************************************************************//**
 * Read a parameter.
 *
 * @param[in]     command Packed command characters, see the XBEE_AT_* names.
 * @param[in,out] request Request tracking the answer.
 *
 * @return SL_STATUS_OK once sent,
 *         SL_STATUS_NULL_POINTER if request is NULL,
 *         SL_STATUS_NOT_READY if the module is not ready,
 *         SL_STATUS_BUSY if another request is in flight,
 *         SL_STATUS_NOT_FOUND if the command is not documented,
 *         SL_STATUS_PERMISSION if it cannot be read,
 *         or the transport error.
 ******************************************************************************/
sl_status_t xbee_at_get(uint16_t command, xbee_at_req_t *request);

/***************************************************************************//**
 * Write a parameter, applied immediately.
 *
 * @param[in]     command Packed command characters.
 * @param[in]     value   Value bytes, big-endian for a numeric parameter.
 * @param[in]     len     Value length.
 * @param[in,out] request Request tracking the answer.
 *
 * @return As xbee_at_get(), plus SL_STATUS_INVALID_RANGE when the value is
 *         outside the documented range.
 ******************************************************************************/
sl_status_t xbee_at_set(uint16_t command,
                        const uint8_t *value,
                        uint16_t len,
                        xbee_at_req_t *request);

/***************************************************************************//**
 * Write a numeric parameter from an unsigned value.
 *
 * The value is written in the width the command expects, big-endian.
 *
 * @param[in]     command Packed command characters.
 * @param[in]     value   Value to write.
 * @param[in,out] request Request tracking the answer.
 *
 * @return As xbee_at_set().
 ******************************************************************************/
sl_status_t xbee_at_set_u32(uint16_t command,
                            uint32_t value,
                            xbee_at_req_t *request);

/***************************************************************************//**
 * Write a parameter to be applied later, on the next apply or on leaving
 * Command mode.
 *
 * @param[in]     command Packed command characters.
 * @param[in]     value   Value bytes.
 * @param[in]     len     Value length.
 * @param[in,out] request Request tracking the answer.
 *
 * @return As xbee_at_set().
 ******************************************************************************/
sl_status_t xbee_at_queue_set(uint16_t command,
                              const uint8_t *value,
                              uint16_t len,
                              xbee_at_req_t *request);

/***************************************************************************//**
 * Run a command that takes no parameter, such as apply, write or reset.
 *
 * @param[in]     command Packed command characters.
 * @param[in,out] request Request tracking the answer.
 *
 * @return As xbee_at_get().
 ******************************************************************************/
sl_status_t xbee_at_exec(uint16_t command, xbee_at_req_t *request);

/***************************************************************************//**
 * Run a command that takes no parameter, allowing it longer to answer.
 *
 * For the commands that work on flash and can take far longer than an ordinary
 * parameter access: writing the configuration, and restoring defaults. The
 * manual warns that nothing may be sent to the module between the write command
 * and its reply (manual lines 7093 to 7095), so giving up early and moving on is
 * exactly what must not happen.
 *
 * @param[in]     command    Packed command characters.
 * @param[in,out] request    Request tracking the answer.
 * @param[in]     timeout_ms How long to wait, 0 for the ordinary timeout.
 *
 * @return As xbee_at_exec().
 ******************************************************************************/
sl_status_t xbee_at_exec_timeout(uint16_t command,
                                 xbee_at_req_t *request,
                                 uint32_t timeout_ms);

/***************************************************************************//**
 * Report whether a request has finished, either way.
 *
 * @param[in] request Request to inspect.
 *
 * @return true when it is done or failed.
 ******************************************************************************/
bool xbee_at_req_complete(const xbee_at_req_t *request);

/***************************************************************************//**
 * Read a completed request's value as a 32-bit unsigned integer.
 *
 * @param[in]  request Completed request.
 * @param[out] value   Decoded value.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if either argument is NULL,
 *         SL_STATUS_INVALID_STATE if the request returned no value,
 *         SL_STATUS_INVALID_RANGE if the value is wider than 32 bits.
 ******************************************************************************/
sl_status_t xbee_at_req_value_u32(const xbee_at_req_t *request, uint32_t *value);

/***************************************************************************//**
 * Read a completed request's value as a 64-bit unsigned integer.
 *
 * @param[in]  request Completed request.
 * @param[out] value   Decoded value.
 *
 * @return As xbee_at_req_value_u32().
 ******************************************************************************/
sl_status_t xbee_at_req_value_u64(const xbee_at_req_t *request, uint64_t *value);

/***************************************************************************//**
 * Read a completed request's value as a NUL-terminated string.
 *
 * @param[in]  request Completed request.
 * @param[out] out     Destination text.
 * @param[in]  cap     Capacity of out, including the terminator.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if either pointer is NULL,
 *         SL_STATUS_INVALID_STATE if the request returned no value,
 *         SL_STATUS_WOULD_OVERFLOW if the value does not fit.
 ******************************************************************************/
sl_status_t xbee_at_req_value_str(const xbee_at_req_t *request,
                                  char *out,
                                  uint16_t cap);

/***************************************************************************//**
 * Change the module's serial mode and follow it.
 *
 * Writes AP and switches the local transport once the module has acknowledged.
 * The change is not persisted: call xbee_at_exec() with the write command
 * separately if it should survive a reset, bearing in mind the module's flash
 * endurance of 10 000 cycles (manual lines 7089 to 7099).
 *
 * @param[in]     mode    Target mode. XBEE_MODE_AUTO is not accepted here.
 * @param[in,out] request Request tracking the change.
 *
 * @return As xbee_at_set(), plus SL_STATUS_INVALID_PARAMETER for
 *         XBEE_MODE_AUTO.
 ******************************************************************************/
sl_status_t xbee_set_mode(xbee_mode_t mode, xbee_at_req_t *request);

/***************************************************************************//**
 * Send a raw API frame. API modes only.
 *
 * @param[in]     frame      Frame to send, its identifier assigned by the transport.
 * @param[in,out] api_request Request tracking the answer, or NULL for none.
 * @param[in]     timeout_ms How long to wait.
 *
 * @return SL_STATUS_OK once sent,
 *         SL_STATUS_NOT_SUPPORTED in Transparent mode,
 *         SL_STATUS_NOT_READY if the module is not ready,
 *         or the transport error.
 ******************************************************************************/
sl_status_t xbee_send_frame(xbee_frame_t *frame,
                            xbee_api_request_t *api_request,
                            uint32_t timeout_ms);

/***************************************************************************//**
 * Send data over the air.
 *
 * In an API mode this builds a Transmit Request to @p addr64. In Transparent
 * mode the bytes are written straight through and the module sends them to
 * whatever DH and DL hold, so naming a different destination is refused.
 *
 * @param[in]     addr64      Destination, XBEE_ADDR64_BROADCAST to broadcast.
 * @param[in]     options     Transmit options, see the XBEE_TX_OPT_* names.
 * @param[in]     data        Payload, at most XBEE_MAX_PAYLOAD_LEN bytes.
 * @param[in]     len         Payload length.
 * @param[in,out] api_request Request tracking the transmit status, or NULL.
 *
 * @return SL_STATUS_OK once sent,
 *         SL_STATUS_NOT_READY if the module is not ready,
 *         SL_STATUS_NOT_SUPPORTED if a specific destination is named in
 *         Transparent mode,
 *         or the transport error.
 ******************************************************************************/
sl_status_t xbee_send_data(uint64_t addr64,
                           uint8_t options,
                           const uint8_t *data,
                           uint16_t len,
                           xbee_api_request_t *api_request);

/***************************************************************************//**
 * Register the callback for received data, in every mode.
 *
 * @param[in] callback Callback, or NULL to discard received data.
 * @param[in] user     Passed back to the callback.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_set_data_callback(xbee_data_cb_t callback, void *user);

/***************************************************************************//**
 * Register the callback for every unsolicited frame. API modes only.
 *
 * @param[in] callback Callback, or NULL.
 * @param[in] user     Passed back to the callback.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_set_frame_callback(xbee_frame_cb_t callback, void *user);

/***************************************************************************//**
 * Register the callback for modem status changes. API modes only.
 *
 * @param[in] callback Callback, or NULL.
 * @param[in] user     Passed back to the callback.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_set_modem_status_callback(xbee_modem_status_cb_t callback,
                                           void *user);

/***************************************************************************//**
 * Force every request through a Command mode session.
 *
 * Command mode is reachable from every operating mode (manual line 3041), so
 * this works whether the module was detected in Transparent mode or in an API
 * mode. Until the session is closed, every xbee_at_* call travels as AT text
 * rather than as an API frame.
 *
 * This exists for configuration work. In Command mode a parameter write is
 * staged and does not take effect until the session ends (manual lines 3101 to
 * 3110), so a batch of writes can include ones that would otherwise break the
 * link under the caller, such as AP or BD, and a write to flash can commit them
 * all at once.
 *
 * Returns at once. Drive xbee_process() and poll xbee_cmd_session_status().
 *
 * @return SL_STATUS_OK once the session is open or the entry sequence has
 *         started,
 *         SL_STATUS_NOT_READY if the module is not ready,
 *         SL_STATUS_BUSY if a request is in flight,
 *         SL_STATUS_INVALID_STATE if a session is already forced,
 *         or the transport error.
 ******************************************************************************/
sl_status_t xbee_cmd_session_open(void);

/***************************************************************************//**
 * Progress of the forced session.
 *
 * @return SL_STATUS_OK when the session is open and a request may be issued,
 *         SL_STATUS_IN_PROGRESS while the entry sequence runs,
 *         SL_STATUS_TIMEOUT if the module never answered the escape sequence,
 *         in which case the session is no longer forced,
 *         SL_STATUS_INVALID_STATE if no session was requested.
 ******************************************************************************/
sl_status_t xbee_cmd_session_status(void);

/***************************************************************************//**
 * Close the forced session and return to the detected mode.
 *
 * Sends the exit command, which applies everything the session staged. Returns
 * at once; drive xbee_process() until xbee_cmd_session_is_open() is false.
 *
 * @return SL_STATUS_OK once the exit has started, or once it was found that
 *         there was nothing left to close,
 *         SL_STATUS_INVALID_STATE if no session is forced,
 *         SL_STATUS_BUSY if a request is in flight,
 *         or the transport error.
 ******************************************************************************/
sl_status_t xbee_cmd_session_close(void);

/***************************************************************************//**
 * Report whether a forced session is open or being opened.
 *
 * Becomes false once the session closes, whether because it was closed, the
 * entry sequence failed, or the module's own Command mode timeout lapsed.
 *
 * @return true while requests are routed through Command mode by force.
 ******************************************************************************/
bool xbee_cmd_session_is_open(void);

/***************************************************************************//**
 * Reset the module with its reset line and run bring-up again.
 *
 * Abandons every request, closes any Command mode session, clears the receive
 * path, pulses the reset line and re-detects the mode.
 *
 * @return SL_STATUS_OK once the reset has started, or the driver error.
 ******************************************************************************/
sl_status_t xbee_hw_reset(void);

/***************************************************************************//**
 * Ask the module to sleep, using the sleep request line.
 *
 * Only meaningful when the module is configured for pin sleep: SM set to 1 or
 * 5, with D8 and D9 routed (manual lines 4703 to 4729). The facade checks the
 * values it read during bring-up and refuses otherwise.
 *
 * Returns at once; drive xbee_process() until the state reaches
 * XBEE_STATE_ASLEEP or the attempt fails.
 *
 * @return SL_STATUS_OK once the request line is asserted,
 *         SL_STATUS_NOT_READY if the module is not ready,
 *         SL_STATUS_INVALID_STATE if a request is in flight or a Command mode
 *         session is open, because the module will not sleep then
 *         (manual lines 4789 to 4811),
 *         SL_STATUS_NOT_SUPPORTED if the module is not configured for pin sleep,
 *         or the driver error.
 ******************************************************************************/
sl_status_t xbee_sleep_enter(void);

/***************************************************************************//**
 * Wake the module by releasing the sleep request line.
 *
 * @return SL_STATUS_OK once the line is released,
 *         SL_STATUS_INVALID_STATE if the module is not asleep,
 *         or the driver error.
 ******************************************************************************/
sl_status_t xbee_sleep_exit(void);

/***************************************************************************//**
 * Read the module's sleep status line.
 *
 * @param[out] awake true when the module reports itself awake.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if awake is NULL,
 *         or the driver error.
 ******************************************************************************/
sl_status_t xbee_is_awake(bool *awake);

#endif  // XBEE_H
