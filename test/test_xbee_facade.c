/***************************************************************************//**
 * @file
 * @brief Unit tests for the xbee facade, driven against a simulated module.
 *
 * The facade is the piece that decides which transport carries a request and
 * when, and two defects in it have reached hardware: a guard time measured from
 * the wrong moment, and a Command mode session that could not be opened after a
 * Transparent mode bring-up. Both were invisible to the transport tests because
 * they live in the facade's own sequencing. This suite exists so that class of
 * defect fails here instead.
 *
 * A small simulator plays the part of the module: it answers the escape
 * sequence after a guard time of silence and replies to AT commands with
 * plausible values, so a whole bring-up can be driven on the host.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "fake_drivers.h"
#include "fake_platform.h"
#include "test_util.h"
#include "xbee.h"

/// Guard time the simulated module requires, in milliseconds.
#define SIM_GUARD_MS  1000U

/// Value the simulated module reports for API Enable.
static uint8_t sim_ap;

/// True once the module has been asked to enter Command mode and agreed.
static bool sim_in_command_mode;

/// Bytes of the transmit capture the simulator has already consumed.
static uint16_t sim_consumed;

/// Consecutive command characters seen with no other traffic between them.
static uint8_t sim_escape_run;

/// Virtual time at which the line last carried a byte from the host.
static uint32_t sim_last_rx_ms;

/// Virtual time, tracked alongside the fake clock so the simulator can measure
/// the silence before the escape sequence.
static uint32_t sim_now_ms;

/// Characters of the command line being assembled.
static char sim_line[64];
static uint16_t sim_line_len;

/// The API frame being assembled, when the module is in an API mode.
static uint8_t sim_frame[64];
static uint16_t sim_frame_len;
static uint16_t sim_frame_want;
static bool sim_in_frame;

/// Commands the module has been asked to run, in order, for the tests to check.
static char sim_log[64][8];
static uint16_t sim_log_count;

/***************************************************************************//**
 * Send text from the simulated module to the code under test.
 ******************************************************************************/
static void sim_reply(const char *text)
{
  fake_uart_feed((const uint8_t *)text, (uint16_t)strlen(text));
}

/***************************************************************************//**
 * Record a command the module was asked to run.
 ******************************************************************************/
static void sim_record(const char *name)
{
  if (sim_log_count < (uint16_t)(sizeof(sim_log) / sizeof(sim_log[0]))) {
    (void)snprintf(sim_log[sim_log_count], sizeof(sim_log[0]), "%s", name);
    sim_log_count++;
  }
}

/***************************************************************************//**
 * Report whether the module was asked to run a command.
 ******************************************************************************/
static bool sim_saw(const char *name)
{
  uint16_t i;

  for (i = 0U; i < sim_log_count; i++) {
    if (strcmp(sim_log[i], name) == 0) {
      return true;
    }
  }

  return false;
}

/***************************************************************************//**
 * Answer one assembled command line.
 ******************************************************************************/
static void sim_handle_line(void)
{
  char name[3];

  sim_line[sim_line_len] = '\0';

  if ((sim_line_len < 4U) || (strncmp(sim_line, "AT", 2) != 0)) {
    sim_reply("ERROR\r");
    return;
  }

  name[0] = sim_line[2];
  name[1] = sim_line[3];
  name[2] = '\0';
  sim_record(name);

  if (sim_line_len > 4U) {
    // A command with a value is a write, which the module acknowledges.
    sim_reply("OK\r");
    return;
  }

  if (strcmp(name, "CN") == 0) {
    sim_in_command_mode = false;
    sim_reply("OK\r");
    return;
  }
  if (strcmp(name, "AP") == 0) {
    char text[8];

    (void)snprintf(text, sizeof(text), "%X\r", (unsigned)sim_ap);
    sim_reply(text);
    return;
  }
  if ((strcmp(name, "WR") == 0) || (strcmp(name, "RE") == 0)
      || (strcmp(name, "R1") == 0) || (strcmp(name, "AC") == 0)) {
    sim_reply("OK\r");
    return;
  }

  // Everything else is a read. The value does not matter to these tests, only
  // that the module answers, so a single zero digit stands in.
  sim_reply("0\r");
}

/***************************************************************************//**
 * Answer one assembled API frame.
 *
 * Only the Local AT Command Request is understood, which is all the facade
 * sends while it is finding its way around the module. Escaping is not modelled,
 * so tests that use an API mode use mode 1; the codec's own tests cover mode 2.
 ******************************************************************************/
static void sim_handle_frame(void)
{
  uint8_t reply[16];
  uint16_t data_len;
  uint16_t i;
  uint8_t sum = 0U;
  char name[3];

  // frame[0] is the type, frame[1] the identifier, frame[2..3] the command.
  if ((sim_frame_len < 4U)
      || ((sim_frame[0] != 0x08U) && (sim_frame[0] != 0x09U))) {
    return;
  }

  name[0] = (char)sim_frame[2];
  name[1] = (char)sim_frame[3];
  name[2] = '\0';
  sim_record(name);

  // Frame data: type, identifier, command, status, then one value byte.
  data_len = 6U;
  reply[0] = 0x7EU;
  reply[1] = 0x00U;
  reply[2] = (uint8_t)data_len;
  reply[3] = 0x88U;              // Local AT Command Response
  reply[4] = sim_frame[1];       // the identifier that was asked
  reply[5] = sim_frame[2];
  reply[6] = sim_frame[3];
  reply[7] = 0x00U;              // status OK
  reply[8] = (strcmp(name, "AP") == 0) ? sim_ap : 0U;

  for (i = 3U; i < (uint16_t)(3U + data_len); i++) {
    sum = (uint8_t)(sum + reply[i]);
  }
  reply[3U + data_len] = (uint8_t)(0xFFU - sum);

  fake_uart_feed(reply, (uint16_t)(4U + data_len));
}

/***************************************************************************//**
 * Consume whatever the code under test has transmitted and answer it.
 ******************************************************************************/
static void sim_poll(void)
{
  const uint8_t *tx = fake_uart_tx_data();
  uint16_t len = fake_uart_tx_len();

  while (sim_consumed < len) {
    uint8_t byte = tx[sim_consumed];

    sim_consumed++;

    // An API mode module outside a Command mode session reads frames.
    if (!sim_in_command_mode && (sim_ap != 0U)) {
      if (sim_in_frame) {
        sim_last_rx_ms = sim_now_ms;
        if (sim_frame_want == 0U) {
          // Collecting the two length bytes.
          sim_frame[sim_frame_len] = byte;
          sim_frame_len++;
          if (sim_frame_len == 2U) {
            sim_frame_want = (uint16_t)(((uint16_t)sim_frame[0] << 8)
                                        | sim_frame[1]);
            sim_frame_len = 0U;
            if (sim_frame_want > (uint16_t)sizeof(sim_frame)) {
              sim_in_frame = false;
            }
          }
          continue;
        }
        sim_frame[sim_frame_len] = byte;
        sim_frame_len++;
        // One more byte than the length: the checksum, which is not verified
        // here because the codec's own tests cover it.
        if (sim_frame_len > sim_frame_want) {
          sim_handle_frame();
          sim_in_frame = false;
        }
        continue;
      }
      if (byte == 0x7EU) {
        sim_in_frame = true;
        sim_frame_len = 0U;
        sim_frame_want = 0U;
        sim_escape_run = 0U;
        sim_last_rx_ms = sim_now_ms;
        continue;
      }
    }

    if (byte == '+') {
      // The escape sequence needs a full guard time of silence before it.
      if ((sim_escape_run == 0U)
          && ((sim_now_ms - sim_last_rx_ms) < SIM_GUARD_MS)) {
        // Not enough silence: the module treats it as ordinary data.
        sim_last_rx_ms = sim_now_ms;
        continue;
      }
      sim_escape_run++;
      sim_last_rx_ms = sim_now_ms;
      if (sim_escape_run == 3U) {
        sim_record("+++");
      }
      continue;
    }

    sim_escape_run = 0U;
    sim_last_rx_ms = sim_now_ms;

    if (!sim_in_command_mode) {
      // Outside a session anything else is data the module would send on air.
      continue;
    }

    if (byte == '\r') {
      sim_handle_line();
      sim_line_len = 0U;
      continue;
    }
    if (sim_line_len < (uint16_t)(sizeof(sim_line) - 1U)) {
      sim_line[sim_line_len] = (char)byte;
      sim_line_len++;
    }
  }

  // The escape sequence is answered once a further guard time has passed with
  // nothing else on the line.
  if ((sim_escape_run >= 3U) && !sim_in_command_mode
      && ((sim_now_ms - sim_last_rx_ms) >= SIM_GUARD_MS)) {
    sim_in_command_mode = true;
    sim_escape_run = 0U;
    sim_line_len = 0U;
    sim_reply("OK\r");
  }
}

/***************************************************************************//**
 * Start a fresh module and facade.
 ******************************************************************************/
static void sim_start(uint8_t ap)
{
  fake_platform_reset();
  fake_drivers_reset();

  sim_ap = ap;
  sim_in_command_mode = false;
  sim_consumed = 0U;
  sim_escape_run = 0U;
  sim_last_rx_ms = 0U;
  sim_now_ms = 0U;
  sim_line_len = 0U;
  sim_log_count = 0U;
  sim_frame_len = 0U;
  sim_frame_want = 0U;
  sim_in_frame = false;
}

/***************************************************************************//**
 * Run the facade and the module for a while.
 *
 * @param[in] ms How much virtual time to cover.
 ******************************************************************************/
static void pump(uint32_t ms)
{
  uint32_t elapsed;

  for (elapsed = 0U; elapsed < ms; elapsed++) {
    (void)xbee_process();
    sim_poll();
    fake_clock_advance_ms(1U);
    sim_now_ms++;
  }
}

/***************************************************************************//**
 * Run until the facade is ready, or give up.
 *
 * Stops on the very iteration readiness appears, so that a caller can act at
 * that exact moment. That matters: the facade closes its own Command mode
 * session as its last act of a Transparent mode bring-up, and the reply to that
 * has not arrived yet.
 *
 * @return true if the facade became ready.
 ******************************************************************************/
static bool pump_until_ready(uint32_t limit_ms)
{
  uint32_t elapsed;

  for (elapsed = 0U; elapsed < limit_ms; elapsed++) {
    (void)xbee_process();
    if (xbee_is_ready()) {
      return true;
    }
    sim_poll();
    fake_clock_advance_ms(1U);
    sim_now_ms++;
  }

  return false;
}

/***************************************************************************//**
 * Bring the facade up against a module in the given mode.
 ******************************************************************************/
static void bring_up(uint8_t ap)
{
  static const xbee_config_t config = {
    .mode = XBEE_MODE_AUTO,
    .power_cycle = true,
    .settle_ms = 0U,
    .probe_timeout_ms = 0U,
  };

  sim_start(ap);
  TEST_ASSERT_EQ_UINT(xbee_init(&config), SL_STATUS_OK);
  TEST_ASSERT(pump_until_ready(30000U));
}

/***************************************************************************//**
 * A Transparent mode module is detected, and the control lines were driven.
 ******************************************************************************/
static void test_bringup_transparent(void)
{
  bring_up(0U);

  TEST_ASSERT_EQ_UINT(xbee_get_mode(), XBEE_MODE_TRANSPARENT);
  TEST_ASSERT(xbee_is_ready());
  TEST_ASSERT(fake_drivers_lines()->powered);
  TEST_ASSERT(sim_saw("+++"));
  TEST_ASSERT(sim_saw("AP"));
}

/***************************************************************************//**
 * A Command mode session can be opened the moment bring-up reports ready.
 *
 * This is the regression test for the defect that stopped provisioning running
 * at all on a Transparent mode module. Bring-up closes the session it used to
 * read the module's parameters and reports ready in the same breath, so the
 * transport is still finishing that exit when the application asks for a
 * session of its own. Opening used to demand an idle transport and failed
 * outright; it now waits for one.
 ******************************************************************************/
static void test_session_opens_right_after_transparent_bringup(void)
{
  uint32_t waited;

  bring_up(0U);

  // No pumping in between: this is the first opportunity the application has.
  TEST_ASSERT_EQ_UINT(xbee_cmd_session_open(), SL_STATUS_OK);

  for (waited = 0U; waited < 15000U; waited++) {
    sl_status_t status = xbee_cmd_session_status();

    if (status != SL_STATUS_IN_PROGRESS) {
      TEST_ASSERT_EQ_UINT(status, SL_STATUS_OK);
      break;
    }
    pump(1U);
  }

  TEST_ASSERT_EQ_UINT(xbee_cmd_session_status(), SL_STATUS_OK);
  TEST_ASSERT(xbee_cmd_session_is_open());
}

/***************************************************************************//**
 * A session opened from an API mode module carries commands as AT text.
 ******************************************************************************/
static void test_session_from_api_mode(void)
{
  xbee_at_req_t req;
  uint32_t waited;

  bring_up(1U);
  TEST_ASSERT_EQ_UINT(xbee_get_mode(), XBEE_MODE_API1);

  TEST_ASSERT_EQ_UINT(xbee_cmd_session_open(), SL_STATUS_OK);
  for (waited = 0U; waited < 15000U; waited++) {
    if (xbee_cmd_session_status() != SL_STATUS_IN_PROGRESS) {
      break;
    }
    pump(1U);
  }
  TEST_ASSERT_EQ_UINT(xbee_cmd_session_status(), SL_STATUS_OK);

  // The module only understands AT text while it is in Command mode, so a
  // reply proves the request did not go out as an API frame.
  sim_log_count = 0U;
  TEST_ASSERT_EQ_UINT(xbee_at_get(XBEE_AT_CH, &req), SL_STATUS_OK);
  for (waited = 0U; waited < 5000U; waited++) {
    if (xbee_at_req_complete(&req)) {
      break;
    }
    pump(1U);
  }
  TEST_ASSERT(xbee_at_req_complete(&req));
  TEST_ASSERT_EQ_UINT(req.result, SL_STATUS_OK);
  TEST_ASSERT(sim_saw("CH"));
}

/***************************************************************************//**
 * Closing a session returns the facade to the mode it detected.
 ******************************************************************************/
static void test_session_closes(void)
{
  uint32_t waited;

  bring_up(1U);

  TEST_ASSERT_EQ_UINT(xbee_cmd_session_open(), SL_STATUS_OK);
  for (waited = 0U; waited < 15000U; waited++) {
    if (xbee_cmd_session_status() != SL_STATUS_IN_PROGRESS) {
      break;
    }
    pump(1U);
  }
  TEST_ASSERT_EQ_UINT(xbee_cmd_session_status(), SL_STATUS_OK);

  sim_log_count = 0U;
  TEST_ASSERT_EQ_UINT(xbee_cmd_session_close(), SL_STATUS_OK);
  for (waited = 0U; waited < 5000U; waited++) {
    if (!xbee_cmd_session_is_open()) {
      break;
    }
    pump(1U);
  }

  TEST_ASSERT(!xbee_cmd_session_is_open());
  TEST_ASSERT(sim_saw("CN"));
  TEST_ASSERT_EQ_UINT(xbee_cmd_session_status(), SL_STATUS_INVALID_STATE);
}

/***************************************************************************//**
 * A module that never answers the escape sequence gives up rather than hanging.
 ******************************************************************************/
static void test_session_open_times_out(void)
{
  uint32_t waited;
  sl_status_t status = SL_STATUS_IN_PROGRESS;

  bring_up(1U);

  // From here the module ignores everything, as one with the wrong command
  // character or a different guard time would.
  sim_in_command_mode = false;
  sim_ap = 1U;

  TEST_ASSERT_EQ_UINT(xbee_cmd_session_open(), SL_STATUS_OK);

  for (waited = 0U; waited < 30000U; waited++) {
    status = xbee_cmd_session_status();
    if (status != SL_STATUS_IN_PROGRESS) {
      break;
    }
    // The module is deaf: drive the facade without letting the simulator
    // answer the escape sequence.
    (void)xbee_process();
    fake_clock_advance_ms(1U);
    sim_now_ms++;
  }

  TEST_ASSERT_EQ_UINT(status, SL_STATUS_TIMEOUT);
  TEST_ASSERT(!xbee_cmd_session_is_open());
}

/***************************************************************************//**
 * Entry point.
 ******************************************************************************/
int main(void)
{
  TEST_RUN(test_bringup_transparent);
  TEST_RUN(test_session_opens_right_after_transparent_bringup);
  TEST_RUN(test_session_from_api_mode);
  TEST_RUN(test_session_closes);
  TEST_RUN(test_session_open_times_out);

  return TEST_SUMMARY();
}
