/***************************************************************************//**
 * @file
 * @brief Unit tests for the transparent bridge's escape sequence detector.
 *
 * The detector is the one part of the bridge that is not transparent, so what
 * matters here is not only that the sequence is recognised but that every byte
 * of every sequence that is not the escape comes out again, in order.
 ******************************************************************************/

#include <string.h>

#include "test_util.h"
#include "xbee_bridge_escape.h"

/// Shorthand for the character under test.
#define ESC  ((uint8_t)XBEE_BRIDGE_ESCAPE_CHAR)

/// A gap long enough to open or close a guard window.
#define GUARD  ((uint32_t)XBEE_BRIDGE_ESCAPE_GUARD_MS)

static xbee_bridge_escape_t esc;

/// Everything the detector has released since the fixture was reset.
static uint8_t released[64];
static uint16_t released_len;

/// Set by any call that reported the sequence complete.
static bool exited;

/***************************************************************************//**
 * Record what one call reported.
 ******************************************************************************/
static void collect(const xbee_bridge_escape_out_t *out)
{
  uint8_t i;

  for (i = 0U; i < out->release_len; i++) {
    if (released_len < (uint16_t)sizeof(released)) {
      released[released_len] = out->release[i];
      released_len++;
    }
  }

  if (out->exit_bridge) {
    exited = true;
  }
}

/***************************************************************************//**
 * Start a session at time zero, with nothing released.
 ******************************************************************************/
static void fixture_reset(void)
{
  released_len = 0U;
  exited = false;
  xbee_bridge_escape_reset(&esc, 0U);
}

/***************************************************************************//**
 * Offer one byte.
 ******************************************************************************/
static void feed(uint8_t byte, uint32_t now_ms)
{
  xbee_bridge_escape_out_t out;

  xbee_bridge_escape_feed(&esc, byte, now_ms, &out);
  collect(&out);
}

/***************************************************************************//**
 * Let time pass without a byte.
 ******************************************************************************/
static void idle(uint32_t now_ms)
{
  xbee_bridge_escape_out_t out;

  xbee_bridge_escape_idle(&esc, now_ms, &out);
  collect(&out);
}

/***************************************************************************//**
 * Assert that exactly these bytes have been released, in this order.
 ******************************************************************************/
static void expect_released(const uint8_t *expected, uint16_t len)
{
  uint16_t i;

  TEST_ASSERT_EQ_UINT(released_len, len);

  for (i = 0U; i < len; i++) {
    if (released[i] != expected[i]) {
      TEST_FAIL("byte %u is 0x%02X, expected 0x%02X",
                (unsigned)i, (unsigned)released[i], (unsigned)expected[i]);
      return;
    }
  }
}

/***************************************************************************//**
 * Ordinary traffic passes through untouched and nothing is ever withheld.
 ******************************************************************************/
static void test_plain_data_passes(void)
{
  static const uint8_t data[] = { 0x7EU, 0x00U, 0x04U, 0x08U, 0x01U, 0x41U };
  uint16_t i;

  fixture_reset();

  for (i = 0U; i < (uint16_t)sizeof(data); i++) {
    feed(data[i], (uint32_t)(10U + i));
  }

  expect_released(data, (uint16_t)sizeof(data));
  TEST_ASSERT(!exited);
}

/***************************************************************************//**
 * The module's own escape sequence is not the bridge's, and must reach it.
 ******************************************************************************/
static void test_plus_plus_plus_is_forwarded(void)
{
  static const uint8_t expected[] = { '+', '+', '+' };

  fixture_reset();

  // Sent exactly as the module wants it: silence, the three characters,
  // silence. None of it is the bridge's business.
  feed('+', GUARD * 2U);
  feed('+', (GUARD * 2U) + 1U);
  feed('+', (GUARD * 2U) + 2U);
  idle(GUARD * 4U);

  expect_released(expected, (uint16_t)sizeof(expected));
  TEST_ASSERT(!exited);
}

/***************************************************************************//**
 * The full sequence with both guard windows leaves the bridge, silently.
 ******************************************************************************/
static void test_full_sequence_exits(void)
{
  uint8_t i;

  fixture_reset();

  for (i = 0U; i < (uint8_t)XBEE_BRIDGE_ESCAPE_COUNT; i++) {
    feed(ESC, GUARD + (uint32_t)i);
  }

  // Still inside the trailing window: nothing has happened yet.
  idle(GUARD + (uint32_t)XBEE_BRIDGE_ESCAPE_COUNT);
  TEST_ASSERT(!exited);
  TEST_ASSERT_EQ_UINT(released_len, 0U);

  idle((GUARD * 2U) + (uint32_t)XBEE_BRIDGE_ESCAPE_COUNT);
  TEST_ASSERT(exited);
  TEST_ASSERT_EQ_UINT(released_len, 0U);
}

/***************************************************************************//**
 * Without the leading silence it is data, however many are sent.
 ******************************************************************************/
static void test_no_leading_guard_is_data(void)
{
  static const uint8_t expected[] = { 0x41U, ESC, ESC, ESC };

  fixture_reset();

  feed(0x41U, 10U);
  feed(ESC, 11U);
  feed(ESC, 12U);
  feed(ESC, 13U);
  idle(GUARD * 4U);

  expect_released(expected, (uint16_t)sizeof(expected));
  TEST_ASSERT(!exited);
}

/***************************************************************************//**
 * A run broken by data releases the run first, then the byte that broke it.
 ******************************************************************************/
static void test_interrupted_run_releases_in_order(void)
{
  static const uint8_t expected[] = { ESC, ESC, 0x42U };

  fixture_reset();

  feed(ESC, GUARD);
  feed(ESC, GUARD + 1U);
  feed(0x42U, GUARD + 2U);

  expect_released(expected, (uint16_t)sizeof(expected));
  TEST_ASSERT(!exited);
}

/***************************************************************************//**
 * A byte inside the trailing window makes the whole sequence payload again.
 ******************************************************************************/
static void test_broken_trailing_guard_releases_all(void)
{
  static const uint8_t expected[] = { ESC, ESC, ESC, 0x43U };

  fixture_reset();

  feed(ESC, GUARD);
  feed(ESC, GUARD + 1U);
  feed(ESC, GUARD + 2U);
  feed(0x43U, GUARD + 3U);

  expect_released(expected, (uint16_t)sizeof(expected));
  TEST_ASSERT(!exited);

  // And the session carries on as normal afterwards.
  feed(0x44U, GUARD + 4U);
  TEST_ASSERT_EQ_UINT(released_len, 5U);
  TEST_ASSERT_EQ_UINT(released[4], 0x44U);
}

/***************************************************************************//**
 * A short run that is simply never finished is handed over once it is clear
 * that it will not be.
 ******************************************************************************/
static void test_incomplete_run_released_on_idle(void)
{
  static const uint8_t expected[] = { ESC };

  fixture_reset();

  feed(ESC, GUARD);

  // Held while the window is open.
  idle(GUARD + 1U);
  TEST_ASSERT_EQ_UINT(released_len, 0U);

  idle(GUARD * 2U);
  expect_released(expected, (uint16_t)sizeof(expected));
  TEST_ASSERT(!exited);
}

/***************************************************************************//**
 * A fourth character inside the trailing window is not a fourth candidate.
 ******************************************************************************/
static void test_extra_character_in_trailing_guard(void)
{
  static const uint8_t expected[] = { ESC, ESC, ESC, ESC };

  fixture_reset();

  feed(ESC, GUARD);
  feed(ESC, GUARD + 1U);
  feed(ESC, GUARD + 2U);
  feed(ESC, GUARD + 3U);
  idle(GUARD + 4U);

  expect_released(expected, (uint16_t)sizeof(expected));
  TEST_ASSERT(!exited);
}

/***************************************************************************//**
 * A second attempt after a failed one still works.
 ******************************************************************************/
static void test_second_attempt_after_failure(void)
{
  uint8_t i;

  fixture_reset();

  // First attempt, spoiled by a byte in the trailing window.
  feed(ESC, GUARD);
  feed(ESC, GUARD + 1U);
  feed(ESC, GUARD + 2U);
  feed(0x45U, GUARD + 3U);
  TEST_ASSERT_EQ_UINT(released_len, 4U);

  // Second attempt, done properly.
  for (i = 0U; i < (uint8_t)XBEE_BRIDGE_ESCAPE_COUNT; i++) {
    feed(ESC, (GUARD * 3U) + (uint32_t)i);
  }
  idle(GUARD * 5U);

  TEST_ASSERT(exited);
  TEST_ASSERT_EQ_UINT(released_len, 4U);
}

/***************************************************************************//**
 * The reset time is taken as the last byte, so an escape sent immediately on
 * entry is data rather than the start of a sequence.
 ******************************************************************************/
static void test_escape_immediately_after_reset_is_data(void)
{
  static const uint8_t expected[] = { ESC };

  fixture_reset();

  feed(ESC, 0U);

  expect_released(expected, (uint16_t)sizeof(expected));
  TEST_ASSERT(!exited);
}

/***************************************************************************//**
 * NULL arguments are refused rather than dereferenced.
 ******************************************************************************/
static void test_null_arguments(void)
{
  xbee_bridge_escape_out_t out;

  fixture_reset();

  xbee_bridge_escape_reset(NULL, 0U);

  out.release_len = 0xAAU;
  out.exit_bridge = true;
  xbee_bridge_escape_feed(NULL, ESC, GUARD, &out);
  TEST_ASSERT_EQ_UINT(out.release_len, 0U);
  TEST_ASSERT(!out.exit_bridge);

  out.release_len = 0xAAU;
  out.exit_bridge = true;
  xbee_bridge_escape_idle(NULL, GUARD, &out);
  TEST_ASSERT_EQ_UINT(out.release_len, 0U);
  TEST_ASSERT(!out.exit_bridge);

  // A NULL output record is the one thing there is nowhere to report, so it
  // only has to not crash.
  xbee_bridge_escape_feed(&esc, ESC, GUARD, NULL);
  xbee_bridge_escape_idle(&esc, GUARD, NULL);
}

/***************************************************************************//**
 * Entry point.
 ******************************************************************************/
int main(void)
{
  TEST_RUN(test_plain_data_passes);
  TEST_RUN(test_plus_plus_plus_is_forwarded);
  TEST_RUN(test_full_sequence_exits);
  TEST_RUN(test_no_leading_guard_is_data);
  TEST_RUN(test_interrupted_run_releases_in_order);
  TEST_RUN(test_broken_trailing_guard_releases_all);
  TEST_RUN(test_incomplete_run_released_on_idle);
  TEST_RUN(test_extra_character_in_trailing_guard);
  TEST_RUN(test_second_attempt_after_failure);
  TEST_RUN(test_escape_immediately_after_reset_is_data);
  TEST_RUN(test_null_arguments);

  return TEST_SUMMARY();
}
