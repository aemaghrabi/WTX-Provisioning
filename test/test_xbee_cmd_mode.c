/***************************************************************************//**
 * @file
 * @brief Unit tests for the xbee_cmd_mode transport, against the fake platform.
 ******************************************************************************/

#include <string.h>

#include "test_util.h"
#include "fake_platform.h"
#include "xbee_cmd_mode.h"
#include "xbee_at_table.h"
#include "xbee_uart.h"

/// Lines handed to a multi-line request during a test.
static char lines[8][XBEE_CMD_MODE_LINE_MAX + 1U];
static uint16_t line_count;

/// Transparent mode data handed to the data callback.
static uint8_t data_seen[64];
static uint16_t data_len;

/***************************************************************************//**
 * Record a reply line.
 ******************************************************************************/
static void on_line(const struct xbee_cmd_request *request,
                    const char *line,
                    uint16_t len,
                    void *user)
{
  (void)request;
  (void)user;

  if (line_count < (uint16_t)(sizeof(lines) / sizeof(lines[0]))) {
    (void)memcpy(lines[line_count], line, len);
    lines[line_count][len] = '\0';
  }
  line_count++;
}

/***************************************************************************//**
 * Record Transparent mode data.
 ******************************************************************************/
static void on_data(const uint8_t *data, uint16_t len, void *user)
{
  (void)user;

  if (((uint32_t)data_len + len) <= sizeof(data_seen)) {
    (void)memcpy(&data_seen[data_len], data, len);
    data_len = (uint16_t)(data_len + len);
  }
}

/***************************************************************************//**
 * Feed a NUL-terminated string as if the module had sent it.
 ******************************************************************************/
static void feed_text(const char *text)
{
  fake_uart_feed((const uint8_t *)text, (uint16_t)strlen(text));
}

/***************************************************************************//**
 * Start from a closed session with the clock at zero.
 ******************************************************************************/
static void fixture_reset(void)
{
  fake_platform_reset();
  line_count = 0U;
  data_len = 0U;
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_init(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_IDLE);
}

/***************************************************************************//**
 * Open a session the way a caller would, and check each step.
 ******************************************************************************/
static void enter_session(void)
{
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_enter(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_GUARD_PRE);

  // Nothing is sent until the guard time has passed.
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 0U);

  // A full guard time alone is not enough: the transport holds the line silent
  // for the configured margin on top, so the module's threshold is passed
  // rather than met exactly.
  fake_clock_advance_ms(1001U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 0U);

  fake_clock_advance_ms(XBEE_CMD_MODE_GUARD_MARGIN_MS + 10U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 3U);
  TEST_ASSERT_EQ_MEM(fake_uart_tx_data(), "+++", 3U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_WAIT_OK);

  feed_text("OK\r");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_ACTIVE);
  TEST_ASSERT(xbee_cmd_mode_is_ready());
  fake_uart_tx_clear();
}

/***************************************************************************//**
 * The line classifier recognises what the module can reply.
 ******************************************************************************/
static void test_classify_line(void)
{
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line("OK", 2U), XBEE_CMD_LINE_OK);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line("ERROR", 5U),
                      XBEE_CMD_LINE_ERROR);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line("", 0U), XBEE_CMD_LINE_EMPTY);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line(NULL, 4U), XBEE_CMD_LINE_EMPTY);

  // Ordinary parameter values.
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line("13A200", 6U),
                      XBEE_CMD_LINE_VALUE);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line("C", 1U), XBEE_CMD_LINE_VALUE);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line("My XBee", 7U),
                      XBEE_CMD_LINE_VALUE);

  // File System errors: an uppercase E, a code, a space, a description
  // (manual lines 5820 to 5826).
  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_classify_line("ENOENT No such file", 19U),
    XBEE_CMD_LINE_FS_ERROR);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line("E1 Bad", 6U),
                      XBEE_CMD_LINE_FS_ERROR);

  // Values that merely begin with E are not errors.
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line("E", 1U), XBEE_CMD_LINE_VALUE);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line("EF12", 4U),
                      XBEE_CMD_LINE_VALUE);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line("E Something", 11U),
                      XBEE_CMD_LINE_VALUE);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line("End Device", 10U),
                      XBEE_CMD_LINE_VALUE);
  // A near miss on the literal.
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line("ERRO", 4U),
                      XBEE_CMD_LINE_VALUE);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_classify_line("OKAY", 4U),
                      XBEE_CMD_LINE_VALUE);
}

/***************************************************************************//**
 * Entry waits out the guard time, sends the sequence and waits for the OK.
 ******************************************************************************/
static void test_enter(void)
{
  fixture_reset();
  enter_session();

  // Entering again while a session is open is refused.
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_enter(), SL_STATUS_INVALID_STATE);
}

/***************************************************************************//**
 * The guard time is measured from the last byte sent to the module.
 ******************************************************************************/
static void test_guard_time_after_transmission(void)
{
  const uint8_t payload[4] = { 'd', 'a', 't', 'a' };

  fixture_reset();

  // Transparent data is sent, then entry is requested straight away.
  fake_clock_advance_ms(5000U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_send_data(payload, 4U), SL_STATUS_OK);
  fake_uart_tx_clear();

  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_enter(), SL_STATUS_OK);

  // Half a guard time later the sequence must still not have been sent.
  fake_clock_advance_ms(500U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 0U);

  fake_clock_advance_ms(600U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 3U);
}

/***************************************************************************//**
 * A transmission made by another transport counts towards the guard time.
 *
 * This is the case that breaks mode detection. The facade probes with an API
 * frame first; when nothing answers, it falls back to the escape sequence. If
 * the guard were measured only from this transport's own writes, the sequence
 * would follow the probe frame almost immediately, the module would see less
 * than a full guard time of silence, and it would ignore the sequence. The
 * symptom is bring-up failing on a module that is in Transparent mode, with
 * nothing received at all.
 ******************************************************************************/
static void test_guard_counts_other_transport(void)
{
  // An AP query, the frame the facade probes with: eight bytes, which take
  // about 8.3 ms to shift out at 9600 baud.
  const uint8_t probe[8] = {
    0x7EU, 0x00U, 0x04U, 0x08U, 0x01U, 0x41U, 0x50U, 0x65U
  };

  fixture_reset();

  // Sent straight through the driver, exactly as the API transport does,
  // without this transport being told.
  TEST_ASSERT_EQ_UINT(xbee_uart_write(probe, 8U), SL_STATUS_OK);
  fake_uart_tx_clear();

  // The facade waits for the probe to time out before falling back.
  fake_clock_advance_ms(1000U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_enter(), SL_STATUS_OK);

  // A full guard time has almost passed since the probe, but not the margin,
  // and not the time the probe itself spent on the wire.
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 0U);

  fake_clock_advance_ms(40U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 0U);

  // Once the full guard plus the margin has elapsed since the probe finished
  // transmitting, the sequence goes out.
  fake_clock_advance_ms(XBEE_CMD_MODE_GUARD_MARGIN_MS + 20U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 3U);
  TEST_ASSERT_EQ_MEM(fake_uart_tx_data(), "+++", 3U);
}

/***************************************************************************//**
 * Entry gives up when the module never answers.
 ******************************************************************************/
static void test_enter_timeout(void)
{
  fixture_reset();

  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_enter(), SL_STATUS_OK);
  fake_clock_advance_ms(1001U + XBEE_CMD_MODE_GUARD_MARGIN_MS);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_WAIT_OK);

  // The module waits a guard time before answering, so the transport allows
  // that plus a margin.
  fake_clock_advance_ms(1400U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_WAIT_OK);

  fake_clock_advance_ms(200U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_IDLE);
}

/***************************************************************************//**
 * A read is written as text and its value is returned.
 ******************************************************************************/
static void test_read_parameter(void)
{
  xbee_cmd_request_t req;
  uint32_t value = 0U;
  uint64_t wide = 0U;

  fixture_reset();
  enter_session();

  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_SH, NULL, 0U, XBEE_CMD_VALUE_NONE, &req,
                       XBEE_CMD_MODE_REPLY_TIMEOUT_MS),
    SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 5U);
  TEST_ASSERT_EQ_MEM(fake_uart_tx_data(), "ATSH\r", 5U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_BUSY);

  feed_text("13A200\r");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);

  TEST_ASSERT(xbee_cmd_mode_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.reply, XBEE_CMD_LINE_VALUE);
  TEST_ASSERT_EQ_STR(req.value, "13A200");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_ACTIVE);

  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_value_u32(&req, &value), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value, 0x13A200U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_value_u64(&req, &wide), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(wide, 0x13A200U);

  {
    uint8_t bytes[4];
    uint16_t len = 0U;
    const uint8_t expected[3] = { 0x13U, 0xA2U, 0x00U };

    TEST_ASSERT_EQ_UINT(
      xbee_cmd_mode_value_bytes(&req, bytes, sizeof(bytes), &len),
      SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(len, 3U);
    TEST_ASSERT_EQ_MEM(bytes, expected, 3U);
  }
}

/***************************************************************************//**
 * A write is acknowledged with OK, and a rejected one with ERROR.
 ******************************************************************************/
static void test_write_parameter(void)
{
  const uint8_t channel[1] = { 0x0CU };
  const char *name = "My XBee";
  xbee_cmd_request_t req;

  fixture_reset();
  enter_session();

  // A numeric parameter goes out as hexadecimal without a prefix.
  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_CH, channel, 1U, XBEE_CMD_VALUE_HEX, &req, 1000U),
    SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 7U);
  TEST_ASSERT_EQ_MEM(fake_uart_tx_data(), "ATCH0C\r", 7U);

  feed_text("OK\r");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.reply, XBEE_CMD_LINE_OK);
  fake_uart_tx_clear();

  // A string parameter goes out as it stands.
  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_NI, (const uint8_t *)name, 7U,
                       XBEE_CMD_VALUE_TEXT, &req, 1000U),
    SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 12U);
  TEST_ASSERT_EQ_MEM(fake_uart_tx_data(), "ATNIMy XBee\r", 12U);

  feed_text("OK\r");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);
  fake_uart_tx_clear();

  // A rejected value answers ERROR (manual lines 3097 to 3099).
  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_CH, channel, 1U, XBEE_CMD_VALUE_HEX, &req, 1000U),
    SL_STATUS_OK);
  feed_text("ERROR\r");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_cmd_mode_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_FAIL);
  TEST_ASSERT_EQ_UINT(req.reply, XBEE_CMD_LINE_ERROR);
  // The session survives a rejected command.
  TEST_ASSERT(xbee_cmd_mode_is_ready());
}

/***************************************************************************//**
 * A File System error is reported apart from an ordinary failure.
 ******************************************************************************/
static void test_file_system_error(void)
{
  xbee_cmd_request_t req;

  fixture_reset();
  enter_session();

  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_FS, (const uint8_t *)"LS x", 4U,
                       XBEE_CMD_VALUE_TEXT, &req, 1000U),
    SL_STATUS_OK);

  feed_text("ENOENT No such file or directory\r");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);

  TEST_ASSERT(xbee_cmd_mode_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_NOT_SUPPORTED);
  TEST_ASSERT_EQ_UINT(req.reply, XBEE_CMD_LINE_FS_ERROR);
  // The named code is kept so the caller can report it.
  TEST_ASSERT_EQ_STR(req.value, "ENOENT No such file or directory");
}

/***************************************************************************//**
 * A command that is never answered expires.
 ******************************************************************************/
static void test_command_timeout(void)
{
  xbee_cmd_request_t req;

  fixture_reset();
  enter_session();

  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_VR, NULL, 0U, XBEE_CMD_VALUE_NONE, &req, 1000U),
    SL_STATUS_OK);

  fake_clock_advance_ms(999U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT(!xbee_cmd_mode_request_complete(&req));

  fake_clock_advance_ms(2U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_cmd_mode_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_TIMEOUT);
  // The session itself is still believed open.
  TEST_ASSERT(xbee_cmd_mode_is_ready());
}

/***************************************************************************//**
 * A multi-line reply is collected until the empty line that closes it.
 ******************************************************************************/
static void test_multiline_reply(void)
{
  xbee_cmd_request_t req;

  fixture_reset();
  enter_session();

  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send_multi(XBEE_AT_ND, NULL, 0U, XBEE_CMD_VALUE_NONE, &req,
                             XBEE_CMD_MODE_MULTILINE_TIMEOUT_MS,
                             on_line, NULL),
    SL_STATUS_OK);
  TEST_ASSERT(req.multiline);

  // One node's fields, each on its own line (manual lines 5008 to 5021).
  feed_text("FFFE\r13A200\r12345678\r28\rMy XBee\r");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT(!xbee_cmd_mode_request_complete(&req));
  TEST_ASSERT_EQ_UINT(line_count, 5U);
  TEST_ASSERT_EQ_STR(lines[0], "FFFE");
  TEST_ASSERT_EQ_STR(lines[4], "My XBee");

  // A second carriage return with nothing between marks the end of the
  // discovery (manual line 5022).
  feed_text("\r");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_cmd_mode_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.line_count, 6U);
  TEST_ASSERT(xbee_cmd_mode_is_ready());
}

/***************************************************************************//**
 * A multi-line reply that no node answers ends as a timeout.
 ******************************************************************************/
static void test_multiline_timeout(void)
{
  xbee_cmd_request_t req;

  fixture_reset();
  enter_session();

  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send_multi(XBEE_AT_ND, NULL, 0U, XBEE_CMD_VALUE_NONE, &req,
                             2500U, on_line, NULL),
    SL_STATUS_OK);

  fake_clock_advance_ms(2600U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_cmd_mode_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_TIMEOUT);

  // With at least one line collected, the same window closing is a success.
  fixture_reset();
  enter_session();
  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send_multi(XBEE_AT_VL, NULL, 0U, XBEE_CMD_VALUE_NONE, &req,
                             2500U, on_line, NULL),
    SL_STATUS_OK);
  feed_text("Build 2024\r");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  fake_clock_advance_ms(2600U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_cmd_mode_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);
}

/***************************************************************************//**
 * The session closes on the exit command.
 ******************************************************************************/
static void test_exit(void)
{
  xbee_cmd_request_t req;

  fixture_reset();
  enter_session();

  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_exit(&req), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 5U);
  TEST_ASSERT_EQ_MEM(fake_uart_tx_data(), "ATCN\r", 5U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_EXITING);

  feed_text("OK\r");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_IDLE);
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);

  // Exiting with no session open is refused.
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_exit(&req), SL_STATUS_INVALID_STATE);
}

/***************************************************************************//**
 * The session is given up shortly before the module's own timeout lapses.
 ******************************************************************************/
static void test_session_timeout(void)
{
  xbee_cmd_request_t req;

  fixture_reset();
  enter_session();

  // Default Command mode timeout is ten seconds (manual lines 6157 to 6164).
  fake_clock_advance_ms(9000U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_cmd_mode_is_ready());

  fake_clock_advance_ms(600U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_IDLE);

  // A command after that is refused rather than sent into a closed window.
  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_SH, NULL, 0U, XBEE_CMD_VALUE_NONE, &req, 1000U),
    SL_STATUS_INVALID_STATE);
}

/***************************************************************************//**
 * Each accepted command pushes the session timeout out again.
 ******************************************************************************/
static void test_session_refreshed_by_commands(void)
{
  xbee_cmd_request_t req;
  uint16_t i;

  fixture_reset();
  enter_session();

  // Five reads spaced eight seconds apart span far more than one timeout
  // window, yet the session stays open throughout.
  for (i = 0U; i < 5U; i++) {
    TEST_ASSERT_EQ_UINT(
      xbee_cmd_mode_send(XBEE_AT_SH, NULL, 0U, XBEE_CMD_VALUE_NONE, &req, 1000U),
      SL_STATUS_OK);
    feed_text("13A200\r");
    TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);
    fake_clock_advance_ms(8000U);
    TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
    TEST_ASSERT(xbee_cmd_mode_is_ready());
  }
}

/***************************************************************************//**
 * A module configured away from the defaults is still reachable.
 ******************************************************************************/
static void test_custom_parameters(void)
{
  fixture_reset();

  // The manual's own example: a guard time of 0x5DC and a command character of
  // 0x31 means typing 111 with 1.5 seconds of silence either side
  // (lines 3046 to 3049).
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_set_params('1', 1500U, 20000U),
                      SL_STATUS_OK);

  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_enter(), SL_STATUS_OK);
  fake_clock_advance_ms(1400U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 0U);

  fake_clock_advance_ms(200U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 3U);
  TEST_ASSERT_EQ_MEM(fake_uart_tx_data(), "111", 3U);

  feed_text("OK\r");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_cmd_mode_is_ready());

  // Parameters cannot be changed under a running session.
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_set_params('+', 1000U, 10000U),
                      SL_STATUS_INVALID_STATE);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_abandon(SL_STATUS_ABORT), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_set_params('+', 0U, 10000U),
                      SL_STATUS_INVALID_PARAMETER);
}

/***************************************************************************//**
 * With no session open, received bytes are Transparent mode radio data.
 ******************************************************************************/
static void test_transparent_data(void)
{
  const uint8_t payload[5] = { 'h', 'e', 'l', 'l', 'o' };

  fixture_reset();
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_set_data_callback(on_data, NULL),
                      SL_STATUS_OK);

  fake_uart_feed(payload, 5U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(data_len, 5U);
  TEST_ASSERT_EQ_MEM(data_seen, payload, 5U);

  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_send_data(payload, 5U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 5U);
  TEST_ASSERT_EQ_MEM(fake_uart_tx_data(), payload, 5U);

  // Once a session is open, the same bytes are Command mode replies instead,
  // and sending raw data is refused.
  fake_uart_tx_clear();
  enter_session();
  data_len = 0U;
  fake_uart_feed(payload, 5U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(data_len, 0U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_send_data(payload, 5U),
                      SL_STATUS_INVALID_STATE);
}

/***************************************************************************//**
 * Abandoning gives up the session without talking to the module.
 ******************************************************************************/
static void test_abandon(void)
{
  xbee_cmd_request_t req;

  fixture_reset();
  enter_session();

  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_SH, NULL, 0U, XBEE_CMD_VALUE_NONE, &req, 1000U),
    SL_STATUS_OK);
  fake_uart_tx_clear();

  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_abandon(SL_STATUS_ABORT), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_IDLE);
  TEST_ASSERT(xbee_cmd_mode_request_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_ABORT);
  // Nothing was sent to the module.
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 0U);
}

/***************************************************************************//**
 * Line assembly copes with splits, line feeds and over-long lines.
 ******************************************************************************/
static void test_line_assembly(void)
{
  xbee_cmd_request_t req;
  char long_line[XBEE_CMD_MODE_LINE_MAX + 32U];
  uint16_t i;

  fixture_reset();
  enter_session();

  // A reply split across several process calls still assembles.
  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_SH, NULL, 0U, XBEE_CMD_VALUE_NONE, &req, 1000U),
    SL_STATUS_OK);
  feed_text("13A");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT(!xbee_cmd_mode_request_complete(&req));
  feed_text("200\r");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_STR(req.value, "13A200");

  // A line feed alongside the carriage return does not create an empty line.
  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_SL, NULL, 0U, XBEE_CMD_VALUE_NONE, &req, 1000U),
    SL_STATUS_OK);
  feed_text("\r\n12345678\r\n");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT(xbee_cmd_mode_request_complete(&req));
  TEST_ASSERT_EQ_STR(req.value, "12345678");

  // A line longer than the buffer is dropped rather than truncated, so the
  // command times out instead of returning a wrong value.
  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_VL, NULL, 0U, XBEE_CMD_VALUE_NONE, &req, 1000U),
    SL_STATUS_OK);
  for (i = 0U; i < (uint16_t)(sizeof(long_line) - 2U); i++) {
    long_line[i] = 'A';
  }
  long_line[sizeof(long_line) - 2U] = '\r';
  long_line[sizeof(long_line) - 1U] = '\0';
  feed_text(long_line);
  do {
    TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  } while (xbee_uart_rx_available() > 0U);
  TEST_ASSERT(!xbee_cmd_mode_request_complete(&req));
  fake_clock_advance_ms(1100U);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_TIMEOUT);
}

/***************************************************************************//**
 * Error paths behave.
 ******************************************************************************/
static void test_error_paths(void)
{
  xbee_cmd_request_t req;
  uint32_t value = 0U;
  uint8_t bytes[4];
  uint16_t len = 0U;

  fixture_reset();

  // A command needs an open session.
  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_SH, NULL, 0U, XBEE_CMD_VALUE_NONE, &req, 1000U),
    SL_STATUS_INVALID_STATE);

  enter_session();

  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_SH, NULL, 0U, XBEE_CMD_VALUE_NONE, NULL, 1000U),
    SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_SH, NULL, 4U, XBEE_CMD_VALUE_HEX, &req, 1000U),
    SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_send_data(NULL, 4U), SL_STATUS_NULL_POINTER);

  // A value too long for the command buffer is refused before anything is sent.
  {
    static uint8_t huge[XBEE_CMD_MODE_CMD_MAX];

    TEST_ASSERT_EQ_UINT(
      xbee_cmd_mode_send(XBEE_AT_NI, huge, (uint16_t)sizeof(huge),
                         XBEE_CMD_VALUE_TEXT, &req, 1000U),
      SL_STATUS_WOULD_OVERFLOW);
    TEST_ASSERT_EQ_UINT(
      xbee_cmd_mode_send(XBEE_AT_FK, huge, (uint16_t)sizeof(huge),
                         XBEE_CMD_VALUE_HEX, &req, 1000U),
      SL_STATUS_WOULD_OVERFLOW);
  }

  // A second command while one is in flight is refused.
  TEST_ASSERT_EQ_UINT(
    xbee_cmd_mode_send(XBEE_AT_SH, NULL, 0U, XBEE_CMD_VALUE_NONE, &req, 1000U),
    SL_STATUS_OK);
  {
    xbee_cmd_request_t other;

    TEST_ASSERT_EQ_UINT(
      xbee_cmd_mode_send(XBEE_AT_SL, NULL, 0U, XBEE_CMD_VALUE_NONE, &other, 1000U),
      SL_STATUS_INVALID_STATE);
  }

  // Values cannot be read from a command that has not completed with one.
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_value_u32(&req, &value),
                      SL_STATUS_INVALID_STATE);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_value_u32(NULL, &value),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_value_bytes(&req, bytes, sizeof(bytes), &len),
                      SL_STATUS_INVALID_STATE);

  feed_text("OK\r");
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  // An OK carries no value to convert.
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_value_u32(&req, &value),
                      SL_STATUS_INVALID_STATE);

  TEST_ASSERT(!xbee_cmd_mode_request_complete(NULL));
}

/***************************************************************************//**
 * Entry survives a transmitter that is momentarily busy.
 ******************************************************************************/
static void test_entry_retries_on_busy(void)
{
  fixture_reset();

  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_enter(), SL_STATUS_OK);
  fake_clock_advance_ms(1001U + XBEE_CMD_MODE_GUARD_MARGIN_MS);

  fake_uart_set_write_status(SL_STATUS_BUSY);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  // Still waiting to send, with nothing transmitted.
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_GUARD_PRE);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 0U);

  fake_uart_set_write_status(SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_process(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_cmd_mode_get_state(), XBEE_CMD_STATE_WAIT_OK);
  TEST_ASSERT_EQ_UINT(fake_uart_tx_len(), 3U);
}

/***************************************************************************//**
 * Entry point.
 ******************************************************************************/
int main(void)
{
  TEST_RUN(test_classify_line);
  TEST_RUN(test_enter);
  TEST_RUN(test_guard_time_after_transmission);
  TEST_RUN(test_guard_counts_other_transport);
  TEST_RUN(test_enter_timeout);
  TEST_RUN(test_read_parameter);
  TEST_RUN(test_write_parameter);
  TEST_RUN(test_file_system_error);
  TEST_RUN(test_command_timeout);
  TEST_RUN(test_multiline_reply);
  TEST_RUN(test_multiline_timeout);
  TEST_RUN(test_exit);
  TEST_RUN(test_session_timeout);
  TEST_RUN(test_session_refreshed_by_commands);
  TEST_RUN(test_custom_parameters);
  TEST_RUN(test_transparent_data);
  TEST_RUN(test_abandon);
  TEST_RUN(test_line_assembly);
  TEST_RUN(test_error_paths);
  TEST_RUN(test_entry_retries_on_busy);

  return TEST_SUMMARY();
}
