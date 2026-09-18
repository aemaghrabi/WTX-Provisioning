/***************************************************************************//**
 * @file
 * @brief Unit tests for the xbee_api transport, against the fake platform.
 ******************************************************************************/

#include "test_util.h"
#include "fake_platform.h"
#include "xbee_api.h"
#include "xbee_at_table.h"
#include "xbee_uart.h"

/// Frames handed to the unsolicited callback during a test.
static xbee_frame_t unsolicited[8];
static uint16_t unsolicited_count;
static void *unsolicited_user_seen;

/// Responses handed to a multi-response request during a test.
static uint16_t multi_count;

/***************************************************************************//**
 * Record an unsolicited frame.
 ******************************************************************************/
static void on_unsolicited(const xbee_frame_t *frame, void *user)
{
  unsolicited_user_seen = user;

  if (unsolicited_count < (uint16_t)(sizeof(unsolicited) / sizeof(unsolicited[0]))) {
    unsolicited[unsolicited_count] = *frame;
  }
  unsolicited_count++;
}

/***************************************************************************//**
 * Count a response to a multi-response request.
 ******************************************************************************/
static void on_multi_response(const struct xbee_api_request *request,
                              const xbee_frame_t *frame,
                              void *user)
{
  (void)request;
  (void)frame;
  (void)user;
  multi_count++;
}

/***************************************************************************//**
 * Start from a known state: empty pipe, clock at zero, API mode 1.
 ******************************************************************************/
static void fixture_reset(bool escaped)
{
  fake_platform_reset();
  unsolicited_count = 0U;
  unsolicited_user_seen = NULL;
  multi_count = 0U;
  TEST_ASSERT_EQ_UINT(xbee_api_init(escaped), SL_STATUS_OK);
}

/***************************************************************************//**
 * Build a Local AT Command Response frame on the wire and feed it in.
 *
 * @param[in] frame_id Identifier to echo back.
 * @param[in] command  Two ASCII characters.
 * @param[in] status   Command status byte.
 * @param[in] value    Value bytes, may be NULL.
 * @param[in] len      Value length.
 ******************************************************************************/
static void feed_at_response(uint8_t frame_id,
                             uint16_t command,
                             uint8_t status,
                             const uint8_t *value,
                             uint16_t len)
{
  // Framing plus the fixed fields plus the largest value a test builds.
  static uint8_t wire[XBEE_API_RESP_BUF_SIZE + 16U];
  // Frame type, frame identifier, two command characters and the status byte,
  // then the value (manual lines 8596 to 8612).
  uint16_t data_len = (uint16_t)(5U + len);
  uint16_t i;
  uint8_t sum = 0U;

  wire[0] = 0x7EU;
  wire[1] = (uint8_t)(data_len >> 8);
  wire[2] = (uint8_t)data_len;
  wire[3] = 0x88U;
  wire[4] = frame_id;
  wire[5] = (uint8_t)(command >> 8);
  wire[6] = (uint8_t)command;
  wire[7] = status;
  for (i = 0U; i < len; i++) {
    wire[8U + i] = value[i];
  }
  for (i = 0U; i < data_len; i++) {
    sum = (uint8_t)(sum + wire[3U + i]);
  }
  wire[3U + data_len] = (uint8_t)(0xFFU - sum);

  fake_uart_feed(wire, (uint16_t)(4U + data_len));
}

/***************************************************************************//**
 * A query is sent, answered and decoded.
 ******************************************************************************/
static void test_send_and_receive(void)
{
  // The manual's temperature query, 7E 00 04 08 01 54 50 52 with identifier 1.
  const uint8_t expected_tx[8] = {
    0x7EU, 0x00U, 0x04U, 0x08U, 0x01U, 0x54U, 0x50U, 0x52U
  };
  const uint8_t value[2] = { 0xFFU, 0xFEU };
  xbee_api_request_t req;
  xbee_frame_t f;

  fixture_reset(false);

  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_TP;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &req, XBEE_API_LOCAL_AT_TIMEOUT_MS),
                      SL_STATUS_OK);

  // The transport assigns the identifier, starting at 1.
  TEST_ASSERT_EQ_UINT(req.frame_id, 1U);
  TEST_ASSERT_EQ_UINT(req.state, XBEE_API_REQ_PENDING);
  TEST_ASSERT_EQ_UINT(req.expect_type, XBEE_FRAME_AT_RESPONSE);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 8U);
  TEST_ASSERT_EQ_MEM(fake_uart_tx_data(), expected_tx, 8U);

  // Nothing has arrived yet.
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(!xbee_api_request_complete(&req));

  feed_at_response(1U, XBEE_AT_TP, 0x00U, value, 2U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);

  TEST_ASSERT(xbee_api_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.state, XBEE_API_REQ_DONE);
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.response_count, 1U);
  TEST_ASSERT_EQ_UINT(req.response.type, XBEE_FRAME_AT_RESPONSE);
  TEST_ASSERT_EQ_UINT(req.response.u.at_response.command, XBEE_AT_TP);
  TEST_ASSERT_EQ_UINT(req.response.u.at_response.status, XBEE_AT_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.response.u.at_response.value_len, 2U);
  TEST_ASSERT_EQ_MEM(req.response.u.at_response.value, value, 2U);

  // The response was copied, so it survives later traffic overwriting the
  // parser buffer.
  feed_at_response(2U, XBEE_AT_SL, 0x00U, value, 2U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.response.u.at_response.command, XBEE_AT_TP);
  TEST_ASSERT_EQ_MEM(req.response.u.at_response.value, value, 2U);
}

/***************************************************************************//**
 * Identifiers are assigned in turn and never collide while in flight.
 ******************************************************************************/
static void test_frame_id_allocation(void)
{
  xbee_api_request_t a;
  xbee_api_request_t b;
  xbee_frame_t f;

  fixture_reset(false);

  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_SH;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &a, 1000U), SL_STATUS_OK);

  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_SL;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &b, 1000U), SL_STATUS_OK);

  TEST_ASSERT_EQ_UINT(a.frame_id, 1U);
  TEST_ASSERT_EQ_UINT(b.frame_id, 2U);

  // Each response reaches only its own request.
  feed_at_response(2U, XBEE_AT_SL, 0x00U, NULL, 0U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(!xbee_api_request_complete(&a));
  TEST_ASSERT(xbee_api_request_complete(&b));
  TEST_ASSERT_EQ_UINT(b.response.u.at_response.command, XBEE_AT_SL);

  feed_at_response(1U, XBEE_AT_SH, 0x00U, NULL, 0U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_api_request_complete(&a));
  TEST_ASSERT_EQ_UINT(a.response.u.at_response.command, XBEE_AT_SH);
}

/***************************************************************************//**
 * Passing no request suppresses the module's answer with identifier 0.
 ******************************************************************************/
static void test_fire_and_forget(void)
{
  xbee_frame_t f;

  fixture_reset(false);

  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_AC;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, NULL, 0U), SL_STATUS_OK);

  // Frame identifier 0 means the module sends no response (manual line 7490).
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 8U);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_data()[4], 0x00U);
}

/***************************************************************************//**
 * A request with no possible answer completes immediately.
 ******************************************************************************/
static void test_no_response_expected(void)
{
  xbee_api_request_t req;
  xbee_frame_t f;

  fixture_reset(false);

  // Secure Session Control is answered by 0xAE, so it does expect one; a raw
  // frame of an unknown type does not.
  xbee_frame_raw_defaults(&f, 0x95U);
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &req, 1000U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.expect_type, 0U);
  TEST_ASSERT(xbee_api_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);
}

/***************************************************************************//**
 * A request that is never answered expires at its deadline.
 ******************************************************************************/
static void test_timeout(void)
{
  xbee_api_request_t req;
  xbee_api_stats_t stats;
  xbee_frame_t f;

  fixture_reset(false);

  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_VR;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &req, 1000U), SL_STATUS_OK);

  // Just short of the deadline the request is still waiting.
  fake_clock_advance_ms(999U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(!xbee_api_request_complete(&req));

  fake_clock_advance_ms(2U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_api_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.state, XBEE_API_REQ_FAILED);
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_TIMEOUT);
  TEST_ASSERT_EQ_UINT(req.response_count, 0U);

  TEST_ASSERT_EQ_UINT(xbee_api_get_stats(&stats), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(stats.timeouts, 1U);

  // The answer arriving late is counted as an orphan, not written into the
  // request the caller may already have reclaimed.
  TEST_ASSERT_EQ_UINT(xbee_api_set_unsolicited_callback(on_unsolicited, NULL),
                      SL_STATUS_OK);
  feed_at_response(1U, XBEE_AT_VR, 0x00U, NULL, 0U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(unsolicited_count, 1U);
  TEST_ASSERT_EQ_UINT(xbee_api_get_stats(&stats), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(stats.responses_orphaned, 1U);
}

/***************************************************************************//**
 * Deadlines stay correct across the tick counter wrap.
 ******************************************************************************/
static void test_timeout_across_wrap(void)
{
  xbee_api_request_t req;
  xbee_frame_t f;

  fixture_reset(false);

  // Start 100 ms before the 32-bit counter wraps.
  fake_clock_set(0xFFFFFF9CU);

  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_VR;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &req, 1000U), SL_STATUS_OK);

  // 500 ms later the counter has wrapped, but the request must still be live.
  fake_clock_advance_ms(500U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(!xbee_api_request_complete(&req));

  fake_clock_advance_ms(600U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_api_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_TIMEOUT);
}

/***************************************************************************//**
 * A discovery collects several responses and ends at its timeout.
 ******************************************************************************/
static void test_multi_response(void)
{
  xbee_api_request_t req;
  xbee_frame_t f;
  const uint8_t node[4] = { 0x12U, 0x34U, 0x56U, 0x78U };

  fixture_reset(false);

  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_ND;
  TEST_ASSERT_EQ_UINT(
    xbee_api_send_multi(&f, &req, 2500U, on_multi_response, NULL),
    SL_STATUS_OK);
  TEST_ASSERT(req.multi);

  // Three nodes answer, each in its own response frame (manual lines 5040 to
  // 5044).
  feed_at_response(1U, XBEE_AT_ND, 0x00U, node, 4U);
  feed_at_response(1U, XBEE_AT_ND, 0x00U, node, 4U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(!xbee_api_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.response_count, 2U);
  TEST_ASSERT_EQ_UINT(multi_count, 2U);

  fake_clock_advance_ms(1000U);
  feed_at_response(1U, XBEE_AT_ND, 0x00U, node, 4U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.response_count, 3U);
  TEST_ASSERT(!xbee_api_request_complete(&req));

  // The timeout is the documented end of a discovery, so it is a success.
  fake_clock_advance_ms(2000U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_api_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.response_count, 3U);

  // With no node answering at all, the same timeout is a failure.
  fixture_reset(false);
  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_ND;
  TEST_ASSERT_EQ_UINT(xbee_api_send_multi(&f, &req, 2500U, NULL, NULL),
                      SL_STATUS_OK);
  fake_clock_advance_ms(2600U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_TIMEOUT);
}

/***************************************************************************//**
 * Frames nobody asked for reach the unsolicited callback.
 ******************************************************************************/
static void test_unsolicited(void)
{
  // Modem Status, hardware reset (manual lines 8805 to 8814).
  const uint8_t modem[6] = { 0x7EU, 0x00U, 0x02U, 0x8AU, 0x00U, 0x75U };
  // An undocumented frame type still reaches the callback, as raw data.
  const uint8_t unknown[7] = { 0x7EU, 0x00U, 0x03U, 0x95U, 0x01U, 0x02U, 0x67U };
  int marker = 0;

  fixture_reset(false);
  TEST_ASSERT_EQ_UINT(xbee_api_set_unsolicited_callback(on_unsolicited, &marker),
                      SL_STATUS_OK);

  fake_uart_feed(modem, 6U);
  fake_uart_feed(unknown, 7U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);

  TEST_ASSERT_EQ_UINT(unsolicited_count, 2U);
  TEST_ASSERT(unsolicited_user_seen == &marker);
  TEST_ASSERT_EQ_UINT(unsolicited[0].type, XBEE_FRAME_MODEM_STATUS);
  TEST_ASSERT_EQ_UINT(unsolicited[0].u.modem_status.status,
                      XBEE_MODEM_HARDWARE_RESET);
  TEST_ASSERT(unsolicited[1].is_raw);
  TEST_ASSERT_EQ_UINT(unsolicited[1].u.raw.frame_type, 0x95U);

  // Removing the callback drops them silently.
  TEST_ASSERT_EQ_UINT(xbee_api_set_unsolicited_callback(NULL, NULL), SL_STATUS_OK);
  fake_uart_feed(modem, 6U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(unsolicited_count, 2U);
}

/***************************************************************************//**
 * API mode 2 escapes on the way out and unescapes on the way in.
 ******************************************************************************/
static void test_escaped_mode(void)
{
  xbee_api_request_t req;
  xbee_frame_t f;
  // A Local AT Command Response carrying 0x7E in its value, which must be
  // escaped on the wire.
  const uint8_t escaped_response[10] = {
    0x7EU, 0x00U, 0x06U, 0x88U, 0x01U, 0x53U, 0x4CU, 0x00U, 0x7DU, 0x5EU
  };
  uint8_t wire[11];
  uint8_t sum = 0U;
  uint16_t i;

  fixture_reset(true);
  TEST_ASSERT(xbee_api_is_escaped());

  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_SL;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &req, 1000U), SL_STATUS_OK);

  // Build the escaped response and append its checksum, which is computed over
  // unescaped data (manual lines 7229 to 7231).
  for (i = 0U; i < 10U; i++) {
    wire[i] = escaped_response[i];
  }
  {
    const uint8_t unescaped_data[5] = { 0x88U, 0x01U, 0x53U, 0x4CU, 0x00U };

    for (i = 0U; i < 5U; i++) {
      sum = (uint8_t)(sum + unescaped_data[i]);
    }
    sum = (uint8_t)(sum + 0x7EU);
  }
  wire[10] = (uint8_t)(0xFFU - sum);
  fake_uart_feed(wire, 11U);

  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_api_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.response.u.at_response.value_len, 1U);
  TEST_ASSERT_EQ_UINT(req.response.u.at_response.value[0], 0x7EU);

  // Switching back to API mode 1 takes effect immediately.
  TEST_ASSERT_EQ_UINT(xbee_api_set_escaped(false), SL_STATUS_OK);
  TEST_ASSERT(!xbee_api_is_escaped());
}

/***************************************************************************//**
 * A response too large for the request buffer fails rather than truncating.
 ******************************************************************************/
static void test_response_overflow(void)
{
  xbee_api_request_t req;
  xbee_frame_t f;
  uint8_t big[XBEE_API_RESP_BUF_SIZE + 8U];
  uint16_t i;

  fixture_reset(false);

  // A pattern that never contains the start delimiter. In API mode 1 a 0x7E in
  // the payload legitimately restarts the frame, which test_delimiter_in_payload
  // covers separately.
  for (i = 0U; i < (uint16_t)sizeof(big); i++) {
    big[i] = (uint8_t)(0x80U + (i & 0x3FU));
  }

  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_VL;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &req, 1000U), SL_STATUS_OK);

  // A value that makes the frame data exceed the request buffer. The frame is
  // longer than one receive chunk, so the transport is pumped until the fake
  // serial link is drained.
  feed_at_response(1U, XBEE_AT_VL, 0x00U, big, XBEE_API_RESP_BUF_SIZE);
  do {
    TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  } while (xbee_uart_rx_available() > 0U);

  TEST_ASSERT(xbee_api_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.state, XBEE_API_REQ_FAILED);
  // Truncating would hand back a silently wrong value.
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_WOULD_OVERFLOW);
}

/***************************************************************************//**
 * Aborting fails every pending request and clears the receive path.
 ******************************************************************************/
static void test_abort(void)
{
  xbee_api_request_t a;
  xbee_api_request_t b;
  xbee_frame_t f;
  uint32_t flushes;

  fixture_reset(false);
  flushes = fake_uart_flush_count();

  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_SH;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &a, 1000U), SL_STATUS_OK);
  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_SL;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &b, 1000U), SL_STATUS_OK);

  TEST_ASSERT_EQ_UINT(xbee_api_abort_all(SL_STATUS_ABORT), SL_STATUS_OK);

  TEST_ASSERT(xbee_api_request_complete(&a));
  TEST_ASSERT(xbee_api_request_complete(&b));
  TEST_ASSERT_EQ_UINT(a.result, SL_STATUS_ABORT);
  TEST_ASSERT_EQ_UINT(b.result, SL_STATUS_ABORT);
  TEST_ASSERT_EQ_UINT(fake_uart_flush_count(), flushes + 1U);

  // Slots were released, so new requests can be sent.
  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_VR;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &a, 1000U), SL_STATUS_OK);
}

/***************************************************************************//**
 * The tracking table and the error paths behave.
 ******************************************************************************/
static void test_error_paths(void)
{
  xbee_api_request_t reqs[XBEE_API_MAX_PENDING + 1U];
  xbee_api_request_t req;
  xbee_frame_t f;
  uint16_t i;

  fixture_reset(false);

  // Filling every slot rejects one more.
  for (i = 0U; i < XBEE_API_MAX_PENDING; i++) {
    xbee_frame_at_defaults(&f);
    f.u.at.command = XBEE_AT_SH;
    TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &reqs[i], 1000U), SL_STATUS_OK);
  }
  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_SH;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &reqs[XBEE_API_MAX_PENDING], 1000U),
                      SL_STATUS_NO_MORE_RESOURCE);

  // Resetting a pending request frees its slot.
  TEST_ASSERT_EQ_UINT(xbee_api_request_reset(&reqs[0]), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(reqs[0].state, XBEE_API_REQ_IDLE);
  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_SH;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &reqs[XBEE_API_MAX_PENDING], 1000U),
                      SL_STATUS_OK);

  // Reusing a request that is still pending is refused.
  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_SL;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &reqs[1], 1000U), SL_STATUS_INVALID_STATE);

  // A failing transmission is reported and tracks nothing.
  fixture_reset(false);
  fake_uart_set_write_status(SL_STATUS_BUSY);
  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_SH;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &req, 1000U), SL_STATUS_BUSY);
  fake_uart_set_write_status(SL_STATUS_OK);

  // A frame that cannot be encoded is reported before anything is sent.
  xbee_frame_tx_request_defaults(&f);
  f.u.tx_request.data = (const uint8_t *)reqs;
  f.u.tx_request.data_len = XBEE_MAX_PAYLOAD_LEN + 1U;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &req, 1000U), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 0U);

  TEST_ASSERT_EQ_UINT(xbee_api_send(NULL, &req, 1000U), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(xbee_api_send_multi(&f, NULL, 1000U, NULL, NULL),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(xbee_api_request_reset(NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(xbee_api_get_stats(NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT(!xbee_api_request_complete(NULL));
}

/***************************************************************************//**
 * A transmit request is matched by its extended status frame.
 ******************************************************************************/
static void test_transmit_status(void)
{
  const uint8_t payload[3] = { 0x41U, 0x42U, 0x43U };
  // Extended Transmit Status for identifier 1, success, no discovery overhead.
  const uint8_t status_frame[11] = {
    0x7EU, 0x00U, 0x07U,
    0x8BU, 0x01U, 0xFFU, 0xFEU, 0x00U, 0x00U, 0x00U,
    0x76U
  };
  xbee_api_request_t req;
  xbee_frame_t f;

  fixture_reset(false);

  xbee_frame_tx_request_defaults(&f);
  f.u.tx_request.dest_addr64 = 0x0013A20012345678ULL;
  f.u.tx_request.data = payload;
  f.u.tx_request.data_len = 3U;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &req, XBEE_API_TX_STATUS_TIMEOUT_MS),
                      SL_STATUS_OK);
  // A transmit request is answered by 0x8B, not 0x89 (manual lines 8817 to 8884).
  TEST_ASSERT_EQ_UINT(req.expect_type, XBEE_FRAME_EXT_TX_STATUS);

  fake_uart_feed(status_frame, 11U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);

  TEST_ASSERT(xbee_api_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.response.u.ext_tx_status.status, XBEE_DELIVERY_SUCCESS);
}

/***************************************************************************//**
 * Noise and corrupt frames are counted and do not disturb a pending request.
 ******************************************************************************/
static void test_stream_robustness(void)
{
  const uint8_t noise[4] = { 0x11U, 0x22U, 0x33U, 0x44U };
  const uint8_t bad_crc[6] = { 0x7EU, 0x00U, 0x02U, 0x8AU, 0x00U, 0x00U };
  xbee_api_request_t req;
  xbee_api_stats_t stats;
  xbee_frame_t f;

  fixture_reset(false);

  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_SH;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &req, 1000U), SL_STATUS_OK);

  fake_uart_feed(noise, 4U);
  fake_uart_feed(bad_crc, 6U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(!xbee_api_request_complete(&req));

  TEST_ASSERT_EQ_UINT(xbee_api_get_stats(&stats), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(stats.frames_bad_checksum, 1U);
  TEST_ASSERT_EQ_UINT(stats.frames_sent, 1U);

  // The real answer still gets through afterwards.
  feed_at_response(1U, XBEE_AT_SH, 0x00U, NULL, 0U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_api_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);

  TEST_ASSERT_EQ_UINT(xbee_api_get_stats(&stats), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(stats.responses_matched, 1U);
  // Only the frame that passed its checksum is counted as received; the
  // corrupt one is counted under frames_bad_checksum instead.
  TEST_ASSERT_EQ_UINT(stats.frames_received, 1U);
}

/***************************************************************************//**
 * A frame split across several process calls still assembles.
 ******************************************************************************/
static void test_split_frame(void)
{
  const uint8_t modem[6] = { 0x7EU, 0x00U, 0x02U, 0x8AU, 0x00U, 0x75U };
  uint16_t i;

  fixture_reset(false);
  TEST_ASSERT_EQ_UINT(xbee_api_set_unsolicited_callback(on_unsolicited, NULL),
                      SL_STATUS_OK);

  // One byte per iteration, as it arrives at 9600 baud.
  for (i = 0U; i < 6U; i++) {
    fake_uart_feed(&modem[i], 1U);
    TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  }

  TEST_ASSERT_EQ_UINT(unsolicited_count, 1U);
  TEST_ASSERT_EQ_UINT(unsolicited[0].type, XBEE_FRAME_MODEM_STATUS);
}

/***************************************************************************//**
 * A payload byte equal to the start delimiter breaks API mode 1 but not mode 2.
 *
 * The manual states that an unescaped 0x7E is always taken as the start of a
 * new frame and everything before it is discarded (lines 7193 to 7195), and
 * that distinguishing a 0x7E in the data from the delimiter is the one real
 * reason to use API mode 2 (lines 7217 to 7222). This test pins that difference
 * down, because it decides which mode the facade should prefer for data that
 * is not under our control.
 ******************************************************************************/
static void test_delimiter_in_payload(void)
{
  const uint8_t value[3] = { 0x11U, 0x7EU, 0x22U };
  xbee_api_request_t req;
  xbee_frame_t f;

  // API mode 1: the embedded delimiter restarts the frame, so the response is
  // lost and the request eventually times out.
  fixture_reset(false);
  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_SL;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &req, 1000U), SL_STATUS_OK);
  feed_at_response(1U, XBEE_AT_SL, 0x00U, value, 3U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(!xbee_api_request_complete(&req));
  fake_clock_advance_ms(1100U);
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_TIMEOUT);

  // API mode 2: the same value is escaped on the wire and arrives intact.
  fixture_reset(true);
  xbee_frame_at_defaults(&f);
  f.u.at.command = XBEE_AT_SL;
  TEST_ASSERT_EQ_UINT(xbee_api_send(&f, &req, 1000U), SL_STATUS_OK);
  {
    // 7E 00 08 88 01 53 4C 00 7D 31 7D 5E 22 <checksum>, with 0x11 and 0x7E
    // escaped. The length and the checksum are computed on unescaped data.
    const uint8_t unescaped[8] = {
      0x88U, 0x01U, 0x53U, 0x4CU, 0x00U, 0x11U, 0x7EU, 0x22U
    };
    uint8_t wire[14];
    uint8_t sum = 0U;
    uint16_t i;

    wire[0] = 0x7EU;
    wire[1] = 0x00U;
    wire[2] = 0x08U;
    wire[3] = 0x88U;
    wire[4] = 0x01U;
    wire[5] = 0x53U;
    wire[6] = 0x4CU;
    wire[7] = 0x00U;
    wire[8] = 0x7DU;
    wire[9] = 0x11U ^ 0x20U;
    wire[10] = 0x7DU;
    wire[11] = 0x7EU ^ 0x20U;
    wire[12] = 0x22U;
    for (i = 0U; i < 8U; i++) {
      sum = (uint8_t)(sum + unescaped[i]);
    }
    wire[13] = (uint8_t)(0xFFU - sum);
    fake_uart_feed(wire, 14U);
  }
  TEST_ASSERT_EQ_UINT(xbee_api_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_api_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.response.u.at_response.value_len, 3U);
  TEST_ASSERT_EQ_MEM(req.response.u.at_response.value, value, 3U);
}

/***************************************************************************//**
 * Entry point.
 ******************************************************************************/
int main(void)
{
  TEST_RUN(test_send_and_receive);
  TEST_RUN(test_frame_id_allocation);
  TEST_RUN(test_fire_and_forget);
  TEST_RUN(test_no_response_expected);
  TEST_RUN(test_timeout);
  TEST_RUN(test_timeout_across_wrap);
  TEST_RUN(test_multi_response);
  TEST_RUN(test_unsolicited);
  TEST_RUN(test_escaped_mode);
  TEST_RUN(test_response_overflow);
  TEST_RUN(test_abort);
  TEST_RUN(test_error_paths);
  TEST_RUN(test_transmit_status);
  TEST_RUN(test_stream_robustness);
  TEST_RUN(test_split_frame);
  TEST_RUN(test_delimiter_in_payload);

  return TEST_SUMMARY();
}
