/***************************************************************************//**
 * @file
 * @brief API-mode transport: frame identifiers, request tracking and dispatch.
 *
 * Sits between xbee_uart and the facade. It assigns frame identifiers, encodes
 * outgoing frames, parses the incoming byte stream, matches responses to the
 * requests that asked for them, times out the rest, and hands everything
 * unsolicited to a callback.
 *
 * It works in API mode 1 and API mode 2 alike; the only difference is the
 * escaping, which is set at init and can be changed when the module's AP value
 * changes.
 *
 * Requests are owned by the caller, so there is no fixed pool and no dynamic
 * allocation. A request must stay alive and unmodified until it completes:
 * @code
 * static xbee_api_request_t req;
 * xbee_frame_t f;
 *
 * xbee_frame_at_defaults(&f);
 * f.u.at.command = XBEE_AT_SH;
 * status = xbee_api_send(&f, &req, XBEE_API_LOCAL_AT_TIMEOUT_MS);
 *
 * // later, once per super-loop iteration
 * xbee_api_process();
 * if (xbee_api_request_complete(&req)) {
 *   // req.result says how it ended; req.response holds the decoded frame
 * }
 * @endcode
 *
 * @note Everything here runs in super-loop context. The only interrupt-context
 *       code in the stack is the UART receive callback inside xbee_uart.
 * @note A decoded response points into the request's own copy of the frame
 *       data, so it stays valid until that request is reused.
 * @note A request object need not be initialised before its first use: send
 *       fills it in completely, and whether one is already in flight is decided
 *       by the transport's own tracking, never by the object's prior contents.
 ******************************************************************************/

#ifndef XBEE_API_H
#define XBEE_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sl_status.h"

#include "xbee_api_config.h"
#include "xbee_frame.h"

/// Progress of one request.
typedef enum {
  XBEE_API_REQ_IDLE,     ///< Never sent, or reset by the caller.
  XBEE_API_REQ_PENDING,  ///< Sent, waiting for a response or a timeout.
  XBEE_API_REQ_DONE,     ///< A response arrived. result is SL_STATUS_OK.
  XBEE_API_REQ_FAILED,   ///< Ended without a usable response. See result.
} xbee_api_req_state_t;

struct xbee_api_request;

/***************************************************************************//**
 * Called for each response to a multi-response request, in super-loop context.
 *
 * @param[in] request The request being answered.
 * @param[in] frame   The decoded response. Valid only for the call.
 * @param[in] user    The value stored in the request.
 ******************************************************************************/
typedef void (*xbee_api_response_cb_t)(const struct xbee_api_request *request,
                                       const xbee_frame_t *frame,
                                       void *user);

/***************************************************************************//**
 * Called for every frame that does not answer a pending request.
 *
 * Covers modem status, received data and I/O samples in both the modern and the
 * legacy families, explicit receive, relay output, extended modem status, and
 * any frame type the manual does not document.
 *
 * @param[in] frame The decoded frame. Valid only for the call, because it points
 *                  into the parser buffer.
 * @param[in] user  The value passed to xbee_api_set_unsolicited_callback().
 ******************************************************************************/
typedef void (*xbee_api_unsolicited_cb_t)(const xbee_frame_t *frame, void *user);

/// One outstanding request. The caller owns it; treat the fields as read only.
typedef struct xbee_api_request {
  xbee_api_req_state_t state;     ///< Progress.
  sl_status_t result;             ///< Outcome once complete. SL_STATUS_OK, SL_STATUS_TIMEOUT, SL_STATUS_ABORT or SL_STATUS_WOULD_OVERFLOW.
  uint8_t  frame_id;              ///< Identifier assigned when sent, 0 when the frame has none.
  uint8_t  request_type;          ///< Frame type that was sent.
  uint8_t  expect_type;           ///< Frame type that answers it, 0 when no answer is expected.
  bool     multi;                 ///< Several responses are expected; the timeout ends the request.
  uint16_t response_count;        ///< Responses received so far.
  uint32_t deadline_tick;         ///< Sleeptimer tick at which the request expires.
  xbee_frame_t response;          ///< Most recent decoded response, valid when response_count is non-zero.
  uint16_t response_len;          ///< Frame data length of that response.
  uint8_t  response_buf[XBEE_API_RESP_BUF_SIZE];  ///< The request's own copy of the frame data.
  xbee_api_response_cb_t on_response;  ///< Optional, called for every response.
  void *user;                     ///< Passed back to on_response.
} xbee_api_request_t;

/***************************************************************************//**
 * Start the transport.
 *
 * Does not touch the UART: call xbee_uart_init() first. Clears any pending
 * request and resets the parser.
 *
 * @param[in] escaped true for API mode 2, false for API mode 1.
 *
 * @return SL_STATUS_OK on success, or the parser initialisation error.
 ******************************************************************************/
sl_status_t xbee_api_init(bool escaped);

/***************************************************************************//**
 * Switch between API mode 1 and API mode 2.
 *
 * Discards any partial frame, because the two encodings cannot be mixed inside
 * one frame. Requests already in flight are left alone; if the module changed
 * mode under them they will time out.
 *
 * @param[in] escaped true for API mode 2.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_api_set_escaped(bool escaped);

/***************************************************************************//**
 * Report the escaping currently in use.
 *
 * @return true when operating in API mode 2.
 ******************************************************************************/
bool xbee_api_is_escaped(void);

/***************************************************************************//**
 * Register the callback for frames that answer no pending request.
 *
 * @param[in] callback Callback, or NULL to drop unsolicited frames.
 * @param[in] user     Passed back to the callback.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_api_set_unsolicited_callback(xbee_api_unsolicited_cb_t callback,
                                              void *user);

/***************************************************************************//**
 * Return a request to its initial state.
 *
 * Use before reusing a request object. A request still pending is removed from
 * the transport first, so its late response becomes unsolicited.
 *
 * @param[in,out] request Request to clear.
 *
 * @return SL_STATUS_OK on success, SL_STATUS_NULL_POINTER if request is NULL.
 ******************************************************************************/
sl_status_t xbee_api_request_reset(xbee_api_request_t *request);

/***************************************************************************//**
 * Send a frame and, when a response is expected, start tracking it.
 *
 * The frame identifier is assigned here, so the caller does not manage them. To
 * suppress the module's response, pass NULL for @p request: the identifier is
 * then set to 0, which the manual defines as no response
 * (docs/manuals/xbee_90002273_ref_manual.md, line 7490).
 *
 * @param[in]     frame      Frame to send. Its frame identifier field is
 *                           overwritten when the type has one.
 * @param[in,out] request    Request to track the answer, or NULL for none.
 * @param[in]     timeout_ms How long to wait for the response.
 *
 * @return SL_STATUS_OK once the bytes are queued,
 *         SL_STATUS_NULL_POINTER if frame is NULL,
 *         SL_STATUS_INVALID_STATE if the request is already pending,
 *         SL_STATUS_NO_MORE_RESOURCE if no tracking slot or frame identifier is
 *         free,
 *         SL_STATUS_BUSY if a transmission is still in flight,
 *         or the encoding error from xbee_frame_encode().
 ******************************************************************************/
sl_status_t xbee_api_send(xbee_frame_t *frame,
                          xbee_api_request_t *request,
                          uint32_t timeout_ms);

/***************************************************************************//**
 * Send a frame that may produce several responses, such as a Network Discover.
 *
 * Each response invokes the request's callback. The request completes when the
 * timeout expires, which is the normal end of the operation rather than a
 * failure, so the result is SL_STATUS_OK whenever at least one response arrived.
 *
 * @param[in]     frame      Frame to send.
 * @param[in,out] request    Request to track the answers. Must not be NULL.
 * @param[in]     timeout_ms Collection window.
 * @param[in]     callback   Called for each response, may be NULL.
 * @param[in]     user       Passed back to the callback.
 *
 * @return As xbee_api_send().
 ******************************************************************************/
sl_status_t xbee_api_send_multi(xbee_frame_t *frame,
                                xbee_api_request_t *request,
                                uint32_t timeout_ms,
                                xbee_api_response_cb_t callback,
                                void *user);

/***************************************************************************//**
 * Drive the transport. Call once per super-loop iteration.
 *
 * Drains the UART into the parser, dispatches complete frames, and expires
 * requests whose deadline has passed. Returns quickly and never blocks.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_api_process(void);

/***************************************************************************//**
 * Report whether a request has finished, either way.
 *
 * @param[in] request Request to inspect.
 *
 * @return true when the request is done or failed, false while it is pending,
 *         idle or NULL.
 ******************************************************************************/
bool xbee_api_request_complete(const xbee_api_request_t *request);

/***************************************************************************//**
 * Fail every pending request.
 *
 * The facade calls this when the module is reset or loses power, so that
 * requests do not sit waiting for answers that can never arrive. Also resets
 * the parser and flushes the receive path.
 *
 * @param[in] result Result to record on each request, normally SL_STATUS_ABORT.
 *
 * @return SL_STATUS_OK on success.
 ******************************************************************************/
sl_status_t xbee_api_abort_all(sl_status_t result);

/// Transport counters, for diagnostics.
typedef struct {
  uint32_t frames_sent;         ///< Frames handed to the UART.
  uint32_t frames_received;     ///< Frames parsed and verified.
  uint32_t frames_bad_checksum; ///< Frames dropped on a checksum mismatch.
  uint32_t frames_dropped;      ///< Frames dropped as too long or resynchronised.
  uint32_t responses_matched;   ///< Frames that answered a pending request.
  uint32_t responses_orphaned;  ///< Response frames with no matching request.
  uint32_t timeouts;            ///< Requests that expired without an answer.
} xbee_api_stats_t;

/***************************************************************************//**
 * Read the transport counters.
 *
 * @param[out] stats Destination.
 *
 * @return SL_STATUS_OK on success, SL_STATUS_NULL_POINTER if stats is NULL.
 ******************************************************************************/
sl_status_t xbee_api_get_stats(xbee_api_stats_t *stats);

#endif  // XBEE_API_H
