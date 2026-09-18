/***************************************************************************//**
 * @file
 * @brief API-mode transport: frame identifiers, request tracking and dispatch.
 ******************************************************************************/

#include <string.h>

#include "sl_sleeptimer.h"

#include "xbee_uart.h"
#include "xbee_api.h"

/// Lowest frame identifier. Zero means no response is expected
/// (docs/manuals/xbee_90002273_ref_manual.md, line 7490).
#define FRAME_ID_MIN  1U

/// Highest frame identifier.
#define FRAME_ID_MAX  255U

/// Parser buffer, holding the frame data of the frame being received.
static uint8_t parse_buf[XBEE_FRAME_MAX_DATA_LEN];

/// Staging buffer for an outgoing encoded frame.
static uint8_t tx_buf[XBEE_FRAME_MAX_ENCODED_LEN];

/// Incoming byte parser.
static xbee_frame_parser_t parser;

/// Requests being tracked. Entries are owned by the caller.
static xbee_api_request_t *pending[XBEE_API_MAX_PENDING];

/// Identifier handed out by the next send that needs one.
static uint8_t next_frame_id = FRAME_ID_MIN;

/// Callback for frames that answer no pending request.
static xbee_api_unsolicited_cb_t unsolicited_cb;

/// Value passed back to unsolicited_cb.
static void *unsolicited_user;

/// Transport counters.
static xbee_api_stats_t stats;

/***************************************************************************//**
 * Report whether a deadline has been reached, tolerating tick counter wrap.
 ******************************************************************************/
static bool tick_reached(uint32_t deadline)
{
  // Signed difference, so the comparison stays correct across the 32-bit wrap
  // as long as the interval is well under half the counter range.
  return ((int32_t)(sl_sleeptimer_get_tick_count() - deadline) >= 0);
}

/***************************************************************************//**
 * Convert a millisecond timeout into an absolute tick deadline.
 ******************************************************************************/
static uint32_t deadline_from_ms(uint32_t timeout_ms)
{
  uint32_t ticks = 0U;

  // sl_sleeptimer_ms_to_tick() takes 16 bits, so longer waits go through the
  // 32-bit conversion.
  if (sl_sleeptimer_ms32_to_tick(timeout_ms, &ticks) != SL_STATUS_OK) {
    ticks = sl_sleeptimer_ms_to_tick((uint16_t)UINT16_MAX);
  }

  return sl_sleeptimer_get_tick_count() + ticks;
}

/***************************************************************************//**
 * Frame type that answers a given request type.
 *
 * @param[in] request_type Type that was sent.
 *
 * @return The answering frame type, or 0 when the module sends none.
 ******************************************************************************/
static uint8_t response_type_for(uint8_t request_type)
{
  uint8_t expect;

  switch (request_type) {
    case XBEE_FRAME_AT:
    case XBEE_FRAME_AT_QUEUE:
      // Local AT Command Response (manual lines 8575 to 8643).
      expect = XBEE_FRAME_AT_RESPONSE;
      break;

    case XBEE_FRAME_REMOTE_AT:
      expect = XBEE_FRAME_REMOTE_AT_RESPONSE;
      break;

    case XBEE_FRAME_TX64:
    case XBEE_FRAME_TX16:
    case XBEE_FRAME_USER_RELAY:
      // Transmit Status (manual lines 8645 to 8746).
      expect = XBEE_FRAME_TX_STATUS;
      break;

    case XBEE_FRAME_TX_REQUEST:
    case XBEE_FRAME_EXPLICIT_TX:
      // Extended Transmit Status (manual lines 8817 to 8884).
      expect = XBEE_FRAME_EXT_TX_STATUS;
      break;

    case XBEE_FRAME_BLE_UNLOCK:
      expect = XBEE_FRAME_BLE_UNLOCK_RESPONSE;
      break;

    case XBEE_FRAME_SECURE_CONTROL:
      expect = XBEE_FRAME_SECURE_RESPONSE;
      break;

    default:
      expect = 0U;
      break;
  }

  return expect;
}

/***************************************************************************//**
 * Find the slot holding a request, or the first free slot.
 *
 * @param[in]  request Request to locate, or NULL to find a free slot.
 * @param[out] index   Slot index when found.
 *
 * @return true when a slot was found.
 ******************************************************************************/
static bool find_slot(const xbee_api_request_t *request, uint16_t *index)
{
  uint16_t i;

  for (i = 0U; i < XBEE_API_MAX_PENDING; i++) {
    if (pending[i] == request) {
      *index = i;
      return true;
    }
  }

  return false;
}

/***************************************************************************//**
 * Report whether a frame identifier is already in flight.
 ******************************************************************************/
static bool frame_id_in_use(uint8_t frame_id)
{
  uint16_t i;

  for (i = 0U; i < XBEE_API_MAX_PENDING; i++) {
    if ((pending[i] != NULL) && (pending[i]->frame_id == frame_id)) {
      return true;
    }
  }

  return false;
}

/***************************************************************************//**
 * Allocate the next free frame identifier.
 *
 * @param[out] frame_id Identifier allocated.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NO_MORE_RESOURCE if every identifier is in flight, which
 *         cannot happen while XBEE_API_MAX_PENDING stays below 255.
 ******************************************************************************/
static sl_status_t allocate_frame_id(uint8_t *frame_id)
{
  uint16_t attempts;

  for (attempts = 0U; attempts < FRAME_ID_MAX; attempts++) {
    uint8_t candidate = next_frame_id;

    next_frame_id = (uint8_t)((next_frame_id == FRAME_ID_MAX)
                              ? FRAME_ID_MIN : (next_frame_id + 1U));

    if (!frame_id_in_use(candidate)) {
      *frame_id = candidate;
      return SL_STATUS_OK;
    }
  }

  return SL_STATUS_NO_MORE_RESOURCE;
}

/***************************************************************************//**
 * Complete a request and release its slot.
 ******************************************************************************/
static void finish_request(uint16_t index, sl_status_t result)
{
  xbee_api_request_t *request = pending[index];

  if (request == NULL) {
    return;
  }

  request->result = result;
  request->state = (result == SL_STATUS_OK) ? XBEE_API_REQ_DONE
                                            : XBEE_API_REQ_FAILED;
  pending[index] = NULL;
}

/***************************************************************************//**
 * Copy a response into a request and decode it against that copy.
 *
 * The parser buffer is reused by the next frame, so a response that must
 * outlive the dispatch has to be copied first.
 *
 * @param[in,out] request Request to fill.
 * @param[in]     data    Frame data.
 * @param[in]     len     Frame data length.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_WOULD_OVERFLOW if the response does not fit,
 *         or the decode error.
 ******************************************************************************/
static sl_status_t store_response(xbee_api_request_t *request,
                                  const uint8_t *data,
                                  uint16_t len)
{
  if (len > (uint16_t)sizeof(request->response_buf)) {
    // Truncating would hand the caller a silently wrong value, so the request
    // fails instead.
    return SL_STATUS_WOULD_OVERFLOW;
  }

  (void)memcpy(request->response_buf, data, len);
  request->response_len = len;

  return xbee_frame_decode(request->response_buf, len, &request->response);
}

/***************************************************************************//**
 * Try to match a decoded frame to a pending request.
 *
 * @param[in] frame Decoded frame, pointing into the parser buffer.
 * @param[in] data  Frame data backing that frame.
 * @param[in] len   Frame data length.
 *
 * @return true when the frame answered a request.
 ******************************************************************************/
static bool dispatch_to_request(const xbee_frame_t *frame,
                                const uint8_t *data,
                                uint16_t len)
{
  uint8_t frame_id = 0U;
  bool has_id = (xbee_frame_get_frame_id(frame, &frame_id) == SL_STATUS_OK);
  uint16_t i;

  for (i = 0U; i < XBEE_API_MAX_PENDING; i++) {
    xbee_api_request_t *request = pending[i];
    sl_status_t status;

    if ((request == NULL) || (request->expect_type != (uint8_t)frame->type)) {
      continue;
    }

    // Frames that carry an identifier must match it. The Bluetooth unlock and
    // secure session responses carry none, so the type alone identifies them.
    if (has_id && (request->frame_id != frame_id)) {
      continue;
    }

    status = store_response(request, data, len);
    if (status != SL_STATUS_OK) {
      finish_request(i, status);
      return true;
    }

    request->response_count++;
    stats.responses_matched++;

    if (request->on_response != NULL) {
      request->on_response(request, &request->response, request->user);
    }

    // A multi-response request keeps collecting until its deadline.
    if (!request->multi) {
      finish_request(i, SL_STATUS_OK);
    }

    return true;
  }

  return false;
}

/***************************************************************************//**
 * Handle one complete frame from the parser.
 ******************************************************************************/
static void handle_frame(const uint8_t *data, uint16_t len)
{
  xbee_frame_t frame;

  stats.frames_received++;

  if (xbee_frame_decode(data, len, &frame) != SL_STATUS_OK) {
    // A frame whose fixed fields are truncated cannot be acted on. It still
    // passed its checksum, so it is counted rather than silently ignored.
    stats.frames_dropped++;
    return;
  }

  if (dispatch_to_request(&frame, data, len)) {
    return;
  }

  // A response frame with nobody waiting is counted separately from ordinary
  // unsolicited traffic: it usually means a request timed out just before its
  // answer arrived.
  switch (frame.type) {
    case XBEE_FRAME_AT_RESPONSE:
    case XBEE_FRAME_REMOTE_AT_RESPONSE:
    case XBEE_FRAME_TX_STATUS:
    case XBEE_FRAME_EXT_TX_STATUS:
    case XBEE_FRAME_BLE_UNLOCK_RESPONSE:
    case XBEE_FRAME_SECURE_RESPONSE:
      stats.responses_orphaned++;
      break;
    default:
      break;
  }

  if (unsolicited_cb != NULL) {
    unsolicited_cb(&frame, unsolicited_user);
  }
}

/***************************************************************************//**
 * Expire every request whose deadline has passed.
 ******************************************************************************/
static void check_deadlines(void)
{
  uint16_t i;

  for (i = 0U; i < XBEE_API_MAX_PENDING; i++) {
    xbee_api_request_t *request = pending[i];

    if ((request == NULL) || !tick_reached(request->deadline_tick)) {
      continue;
    }

    if (request->multi && (request->response_count > 0U)) {
      // For a discovery the timeout is the documented end of the operation,
      // not a failure (manual lines 4976 to 4985).
      finish_request(i, SL_STATUS_OK);
    } else {
      stats.timeouts++;
      finish_request(i, SL_STATUS_TIMEOUT);
    }
  }
}

/***************************************************************************//**
 * Start the transport.
 ******************************************************************************/
sl_status_t xbee_api_init(bool escaped)
{
  uint16_t i;

  for (i = 0U; i < XBEE_API_MAX_PENDING; i++) {
    pending[i] = NULL;
  }

  next_frame_id = FRAME_ID_MIN;
  unsolicited_cb = NULL;
  unsolicited_user = NULL;
  (void)memset(&stats, 0, sizeof(stats));

  return xbee_frame_parser_init(&parser, parse_buf, sizeof(parse_buf), escaped);
}

/***************************************************************************//**
 * Switch between API mode 1 and API mode 2.
 ******************************************************************************/
sl_status_t xbee_api_set_escaped(bool escaped)
{
  return xbee_frame_parser_set_escaped(&parser, escaped);
}

/***************************************************************************//**
 * Report the escaping currently in use.
 ******************************************************************************/
bool xbee_api_is_escaped(void)
{
  return parser.escaped;
}

/***************************************************************************//**
 * Register the unsolicited frame callback.
 ******************************************************************************/
sl_status_t xbee_api_set_unsolicited_callback(xbee_api_unsolicited_cb_t callback,
                                              void *user)
{
  unsolicited_cb = callback;
  unsolicited_user = user;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Return a request to its initial state.
 ******************************************************************************/
sl_status_t xbee_api_request_reset(xbee_api_request_t *request)
{
  uint16_t index = 0U;

  if (request == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  // Stop tracking it first, so a late response is treated as unsolicited
  // instead of writing into a request the caller has reclaimed.
  if (find_slot(request, &index)) {
    pending[index] = NULL;
  }

  (void)memset(request, 0, sizeof(*request));
  request->state = XBEE_API_REQ_IDLE;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Send a frame, optionally tracking its answer.
 ******************************************************************************/
static sl_status_t api_send(xbee_frame_t *frame,
                            xbee_api_request_t *request,
                            uint32_t timeout_ms,
                            bool multi,
                            xbee_api_response_cb_t callback,
                            void *user)
{
  uint16_t slot = 0U;
  uint16_t len = 0U;
  uint8_t frame_id = 0U;
  uint8_t request_type;
  uint8_t expect_type;
  bool has_frame_id;
  sl_status_t status;

  if (frame == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  // Whether a request is already in flight is answered by the tracking table,
  // not by the request's own state field: a caller may hand over a request
  // object that has never been initialised, and reading its fields then would
  // be undefined.
  if ((request != NULL) && find_slot(request, &slot)) {
    return SL_STATUS_INVALID_STATE;
  }

  request_type = frame->is_raw ? frame->u.raw.frame_type : (uint8_t)frame->type;
  expect_type = response_type_for(request_type);
  has_frame_id = !frame->is_raw && xbee_frame_has_frame_id(frame->type);

  if (request != NULL) {
    if (!find_slot(NULL, &slot)) {
      return SL_STATUS_NO_MORE_RESOURCE;
    }
    if (has_frame_id) {
      status = allocate_frame_id(&frame_id);
      if (status != SL_STATUS_OK) {
        return status;
      }
    }
  }

  if (has_frame_id) {
    // Identifier 0 tells the module not to answer, which is what a caller that
    // passed no request wants.
    status = xbee_frame_set_frame_id(frame, frame_id);
    if (status != SL_STATUS_OK) {
      return status;
    }
  }

  status = xbee_frame_encode(frame, parser.escaped, tx_buf, sizeof(tx_buf), &len);
  if (status != SL_STATUS_OK) {
    return status;
  }

  status = xbee_uart_write(tx_buf, len);
  if (status != SL_STATUS_OK) {
    return status;
  }

  stats.frames_sent++;

  if (request == NULL) {
    return SL_STATUS_OK;
  }

  (void)memset(request, 0, sizeof(*request));
  request->state = XBEE_API_REQ_PENDING;
  request->result = SL_STATUS_IN_PROGRESS;
  request->frame_id = frame_id;
  request->request_type = request_type;
  request->expect_type = expect_type;
  request->multi = multi;
  request->deadline_tick = deadline_from_ms(timeout_ms);
  request->on_response = callback;
  request->user = user;

  if (expect_type == 0U) {
    // Nothing will answer, so the request is complete as soon as it is sent.
    request->state = XBEE_API_REQ_DONE;
    request->result = SL_STATUS_OK;
    return SL_STATUS_OK;
  }

  pending[slot] = request;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Send a frame and track a single response.
 ******************************************************************************/
sl_status_t xbee_api_send(xbee_frame_t *frame,
                          xbee_api_request_t *request,
                          uint32_t timeout_ms)
{
  return api_send(frame, request, timeout_ms, false, NULL, NULL);
}

/***************************************************************************//**
 * Send a frame and collect several responses until the timeout.
 ******************************************************************************/
sl_status_t xbee_api_send_multi(xbee_frame_t *frame,
                                xbee_api_request_t *request,
                                uint32_t timeout_ms,
                                xbee_api_response_cb_t callback,
                                void *user)
{
  if (request == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  return api_send(frame, request, timeout_ms, true, callback, user);
}

/***************************************************************************//**
 * Drive the transport.
 ******************************************************************************/
sl_status_t xbee_api_process(void)
{
  uint8_t chunk[XBEE_API_RX_CHUNK];
  uint16_t count = 0U;
  uint16_t i;
  sl_status_t status;

  status = xbee_uart_read(chunk, (uint16_t)sizeof(chunk), &count);
  if (status != SL_STATUS_OK) {
    return status;
  }

  for (i = 0U; i < count; i++) {
    sl_status_t parse = xbee_frame_parser_feed(&parser, chunk[i]);

    if (parse == SL_STATUS_OK) {
      handle_frame(parser.buf, parser.len);
    } else if (parse == SL_STATUS_INVALID_SIGNATURE) {
      stats.frames_bad_checksum++;
    } else if (parse == SL_STATUS_WOULD_OVERFLOW) {
      stats.frames_dropped++;
    } else {
      // SL_STATUS_IN_PROGRESS: the frame is not complete yet.
    }
  }

  check_deadlines();

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Report whether a request has finished.
 ******************************************************************************/
bool xbee_api_request_complete(const xbee_api_request_t *request)
{
  if (request == NULL) {
    return false;
  }

  return ((request->state == XBEE_API_REQ_DONE)
          || (request->state == XBEE_API_REQ_FAILED));
}

/***************************************************************************//**
 * Fail every pending request.
 ******************************************************************************/
sl_status_t xbee_api_abort_all(sl_status_t result)
{
  uint16_t i;

  for (i = 0U; i < XBEE_API_MAX_PENDING; i++) {
    if (pending[i] != NULL) {
      finish_request(i, result);
    }
  }

  (void)xbee_frame_parser_reset(&parser);
  (void)xbee_uart_rx_flush();

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Read the transport counters.
 ******************************************************************************/
sl_status_t xbee_api_get_stats(xbee_api_stats_t *out)
{
  if (out == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  *out = stats;
  // The parser owns its own tallies; fold them in so one snapshot covers the
  // whole receive path.
  out->frames_bad_checksum = parser.frames_bad_crc;
  out->frames_dropped = parser.frames_dropped;

  return SL_STATUS_OK;
}
