/***************************************************************************//**
 * @file
 * @brief Unit tests for the ring_buffer utility.
 ******************************************************************************/

#include "test_util.h"
#include "ring_buffer.h"

/// Capacity used by most cases. Holds capacity - 1 bytes.
#define CAP  8U

static uint8_t storage[CAP];
static ring_buffer_t rb;

/***************************************************************************//**
 * Reset the fixture to an empty buffer of capacity CAP.
 ******************************************************************************/
static void fixture_reset(void)
{
  TEST_ASSERT_EQ_UINT(ring_buffer_init(&rb, storage, CAP), SL_STATUS_OK);
}

/***************************************************************************//**
 * Init rejects bad arguments and accepts only powers of two.
 ******************************************************************************/
static void test_init_validation(void)
{
  TEST_ASSERT_EQ_UINT(ring_buffer_init(NULL, storage, CAP), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(ring_buffer_init(&rb, NULL, CAP), SL_STATUS_NULL_POINTER);

  // Not a power of two.
  TEST_ASSERT_EQ_UINT(ring_buffer_init(&rb, storage, 6U), SL_STATUS_INVALID_PARAMETER);
  // Below the minimum.
  TEST_ASSERT_EQ_UINT(ring_buffer_init(&rb, storage, 1U), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(ring_buffer_init(&rb, storage, 0U), SL_STATUS_INVALID_PARAMETER);
  // Above the maximum.
  TEST_ASSERT_EQ_UINT(ring_buffer_init(&rb, storage, 65535U), SL_STATUS_INVALID_PARAMETER);

  TEST_ASSERT_EQ_UINT(ring_buffer_init(&rb, storage, 2U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(ring_buffer_init(&rb, storage, CAP), SL_STATUS_OK);
}

/***************************************************************************//**
 * A fresh buffer is empty and reports the full free space.
 ******************************************************************************/
static void test_empty_state(void)
{
  uint8_t byte = 0xAAU;

  fixture_reset();

  TEST_ASSERT_EQ_UINT(ring_buffer_count(&rb), 0U);
  TEST_ASSERT_EQ_UINT(ring_buffer_free(&rb), CAP - 1U);
  TEST_ASSERT_EQ_UINT(ring_buffer_pop(&rb, &byte), SL_STATUS_EMPTY);
  // The output is left untouched on an empty pop.
  TEST_ASSERT_EQ_UINT(byte, 0xAAU);
}

/***************************************************************************//**
 * Bytes come back in the order they went in.
 ******************************************************************************/
static void test_push_pop_order(void)
{
  uint8_t byte = 0U;
  uint8_t i;

  fixture_reset();

  for (i = 1U; i <= 5U; i++) {
    TEST_ASSERT_EQ_UINT(ring_buffer_push(&rb, i), SL_STATUS_OK);
  }
  TEST_ASSERT_EQ_UINT(ring_buffer_count(&rb), 5U);
  TEST_ASSERT_EQ_UINT(ring_buffer_free(&rb), (CAP - 1U) - 5U);

  for (i = 1U; i <= 5U; i++) {
    TEST_ASSERT_EQ_UINT(ring_buffer_pop(&rb, &byte), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(byte, i);
  }
  TEST_ASSERT_EQ_UINT(ring_buffer_count(&rb), 0U);
}

/***************************************************************************//**
 * One slot is reserved, so the usable capacity is capacity - 1.
 ******************************************************************************/
static void test_full(void)
{
  uint8_t byte = 0U;
  uint8_t i;

  fixture_reset();

  for (i = 0U; i < (CAP - 1U); i++) {
    TEST_ASSERT_EQ_UINT(ring_buffer_push(&rb, i), SL_STATUS_OK);
  }
  TEST_ASSERT_EQ_UINT(ring_buffer_count(&rb), CAP - 1U);
  TEST_ASSERT_EQ_UINT(ring_buffer_free(&rb), 0U);

  // The overflowing byte is dropped, and nothing already stored is disturbed.
  TEST_ASSERT_EQ_UINT(ring_buffer_push(&rb, 0xFFU), SL_STATUS_FULL);
  TEST_ASSERT_EQ_UINT(ring_buffer_count(&rb), CAP - 1U);

  TEST_ASSERT_EQ_UINT(ring_buffer_pop(&rb, &byte), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(byte, 0U);
  // One slot freed, one push accepted.
  TEST_ASSERT_EQ_UINT(ring_buffer_push(&rb, 0x5AU), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(ring_buffer_push(&rb, 0x5BU), SL_STATUS_FULL);
}

/***************************************************************************//**
 * The indices wrap cleanly over many cycles.
 ******************************************************************************/
static void test_wrap(void)
{
  uint8_t byte = 0U;
  uint16_t i;

  fixture_reset();

  // Far more iterations than the capacity, so the indices wrap repeatedly.
  for (i = 0U; i < 1000U; i++) {
    TEST_ASSERT_EQ_UINT(ring_buffer_push(&rb, (uint8_t)i), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(ring_buffer_pop(&rb, &byte), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(byte, (uint8_t)i);
  }
  TEST_ASSERT_EQ_UINT(ring_buffer_count(&rb), 0U);
}

/***************************************************************************//**
 * Bulk read copies what is available, bounded by the destination.
 ******************************************************************************/
static void test_read(void)
{
  const uint8_t expected[5] = { 10U, 11U, 12U, 13U, 14U };
  uint8_t dst[8];
  uint16_t count = 0xFFFFU;
  uint8_t i;

  fixture_reset();

  // Empty buffer: a valid copy of nothing.
  TEST_ASSERT_EQ_UINT(ring_buffer_read(&rb, dst, sizeof(dst), &count), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(count, 0U);

  for (i = 0U; i < 5U; i++) {
    TEST_ASSERT_EQ_UINT(ring_buffer_push(&rb, expected[i]), SL_STATUS_OK);
  }

  // Destination smaller than the content: partial copy, rest stays buffered.
  TEST_ASSERT_EQ_UINT(ring_buffer_read(&rb, dst, 3U, &count), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(count, 3U);
  TEST_ASSERT_EQ_MEM(dst, expected, 3U);
  TEST_ASSERT_EQ_UINT(ring_buffer_count(&rb), 2U);

  TEST_ASSERT_EQ_UINT(ring_buffer_read(&rb, dst, sizeof(dst), &count), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(count, 2U);
  TEST_ASSERT_EQ_MEM(dst, &expected[3], 2U);
  TEST_ASSERT_EQ_UINT(ring_buffer_count(&rb), 0U);
}

/***************************************************************************//**
 * Bulk read works across the wrap point.
 ******************************************************************************/
static void test_read_across_wrap(void)
{
  const uint8_t expected[4] = { 0xA1U, 0xA2U, 0xA3U, 0xA4U };
  uint8_t dst[8];
  uint16_t count = 0U;
  uint8_t i;

  fixture_reset();

  // Push and drain five bytes so the tail sits near the end of the storage.
  for (i = 0U; i < 5U; i++) {
    TEST_ASSERT_EQ_UINT(ring_buffer_push(&rb, 0U), SL_STATUS_OK);
  }
  TEST_ASSERT_EQ_UINT(ring_buffer_read(&rb, dst, sizeof(dst), &count), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(count, 5U);

  for (i = 0U; i < 4U; i++) {
    TEST_ASSERT_EQ_UINT(ring_buffer_push(&rb, expected[i]), SL_STATUS_OK);
  }
  TEST_ASSERT_EQ_UINT(ring_buffer_read(&rb, dst, sizeof(dst), &count), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(count, 4U);
  TEST_ASSERT_EQ_MEM(dst, expected, 4U);
}

/***************************************************************************//**
 * Clear discards everything and leaves the buffer usable.
 ******************************************************************************/
static void test_clear(void)
{
  uint8_t byte = 0U;

  fixture_reset();

  TEST_ASSERT_EQ_UINT(ring_buffer_push(&rb, 1U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(ring_buffer_push(&rb, 2U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(ring_buffer_clear(&rb), SL_STATUS_OK);

  TEST_ASSERT_EQ_UINT(ring_buffer_count(&rb), 0U);
  TEST_ASSERT_EQ_UINT(ring_buffer_free(&rb), CAP - 1U);
  TEST_ASSERT_EQ_UINT(ring_buffer_pop(&rb, &byte), SL_STATUS_EMPTY);

  TEST_ASSERT_EQ_UINT(ring_buffer_push(&rb, 3U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(ring_buffer_pop(&rb, &byte), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(byte, 3U);
}

/***************************************************************************//**
 * Every entry point rejects NULL.
 ******************************************************************************/
static void test_null_arguments(void)
{
  uint8_t byte = 0U;
  uint8_t dst[2];
  uint16_t count = 0U;

  fixture_reset();

  TEST_ASSERT_EQ_UINT(ring_buffer_push(NULL, 0U), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(ring_buffer_pop(NULL, &byte), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(ring_buffer_pop(&rb, NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(ring_buffer_read(NULL, dst, sizeof(dst), &count),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(ring_buffer_read(&rb, NULL, sizeof(dst), &count),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(ring_buffer_read(&rb, dst, sizeof(dst), NULL),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(ring_buffer_clear(NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(ring_buffer_count(NULL), 0U);
  TEST_ASSERT_EQ_UINT(ring_buffer_free(NULL), 0U);
}

/***************************************************************************//**
 * Entry point.
 ******************************************************************************/
int main(void)
{
  TEST_RUN(test_init_validation);
  TEST_RUN(test_empty_state);
  TEST_RUN(test_push_pop_order);
  TEST_RUN(test_full);
  TEST_RUN(test_wrap);
  TEST_RUN(test_read);
  TEST_RUN(test_read_across_wrap);
  TEST_RUN(test_clear);
  TEST_RUN(test_null_arguments);

  return TEST_SUMMARY();
}
