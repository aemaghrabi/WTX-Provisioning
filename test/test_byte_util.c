/***************************************************************************//**
 * @file
 * @brief Unit tests for the byte_util utility.
 ******************************************************************************/

#include "test_util.h"
#include "byte_util.h"

/***************************************************************************//**
 * Big-endian reads decode the manual's byte order.
 ******************************************************************************/
static void test_read_be(void)
{
  // A 64-bit XBee address as it appears on the wire, manual line 7266.
  const uint8_t addr[8] = {
    0x00U, 0x13U, 0xA2U, 0x00U, 0x12U, 0x34U, 0x56U, 0x78U
  };

  TEST_ASSERT_EQ_UINT(byte_util_read_be16(addr), 0x0013U);
  TEST_ASSERT_EQ_UINT(byte_util_read_be32(addr), 0x0013A200U);
  TEST_ASSERT_EQ_UINT(byte_util_read_be64(addr), 0x0013A20012345678ULL);
  TEST_ASSERT_EQ_UINT(byte_util_read_be32(&addr[4]), 0x12345678U);
}

/***************************************************************************//**
 * Big-endian writes round trip with the reads.
 ******************************************************************************/
static void test_write_be(void)
{
  const uint8_t expected16[2] = { 0xFFU, 0xFEU };
  const uint8_t expected32[4] = { 0x00U, 0x13U, 0xA2U, 0x00U };
  const uint8_t expected64[8] = {
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU
  };
  uint8_t buf[8];

  byte_util_write_be16(buf, 0xFFFEU);
  TEST_ASSERT_EQ_MEM(buf, expected16, 2U);

  byte_util_write_be32(buf, 0x0013A200U);
  TEST_ASSERT_EQ_MEM(buf, expected32, 4U);

  // The broadcast address, manual line 7362.
  byte_util_write_be64(buf, 0x000000000000FFFFULL);
  TEST_ASSERT_EQ_MEM(buf, expected64, 8U);

  byte_util_write_be64(buf, 0x0013A20012345678ULL);
  TEST_ASSERT_EQ_UINT(byte_util_read_be64(buf), 0x0013A20012345678ULL);
}

/***************************************************************************//**
 * Hexadecimal encoding is uppercase, unprefixed and NUL terminated.
 ******************************************************************************/
static void test_hex_encode(void)
{
  const uint8_t in[4] = { 0x00U, 0x13U, 0xA2U, 0xFFU };
  char out[16];

  TEST_ASSERT_EQ_UINT(byte_util_hex_encode(in, 4U, out, sizeof(out)), SL_STATUS_OK);
  TEST_ASSERT_EQ_STR(out, "0013A2FF");

  // Exactly the required capacity: two digits per byte plus the terminator.
  TEST_ASSERT_EQ_UINT(byte_util_hex_encode(in, 4U, out, 9U), SL_STATUS_OK);
  TEST_ASSERT_EQ_STR(out, "0013A2FF");
  TEST_ASSERT_EQ_UINT(byte_util_hex_encode(in, 4U, out, 8U), SL_STATUS_WOULD_OVERFLOW);

  // Zero bytes still produce an empty, terminated string.
  TEST_ASSERT_EQ_UINT(byte_util_hex_encode(NULL, 0U, out, sizeof(out)), SL_STATUS_OK);
  TEST_ASSERT_EQ_STR(out, "");

  TEST_ASSERT_EQ_UINT(byte_util_hex_encode(in, 4U, NULL, sizeof(out)),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(byte_util_hex_encode(NULL, 4U, out, sizeof(out)),
                      SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * Hexadecimal decoding accepts the forms the module accepts.
 *
 * The manual states parameters may be given with or without a leading 0x,
 * for example FFFF or 0xFFFF (lines 3094 to 3096).
 ******************************************************************************/
static void test_hex_decode(void)
{
  const uint8_t expected[2] = { 0xFFU, 0xFFU };
  const uint8_t expected_odd[2] = { 0x0AU, 0xBCU };
  uint8_t out[4];
  uint16_t len = 0U;

  TEST_ASSERT_EQ_UINT(byte_util_hex_decode("FFFF", 4U, out, sizeof(out), &len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(len, 2U);
  TEST_ASSERT_EQ_MEM(out, expected, 2U);

  TEST_ASSERT_EQ_UINT(byte_util_hex_decode("0xFFFF", 6U, out, sizeof(out), &len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(len, 2U);
  TEST_ASSERT_EQ_MEM(out, expected, 2U);

  TEST_ASSERT_EQ_UINT(byte_util_hex_decode("0XffFF", 6U, out, sizeof(out), &len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_MEM(out, expected, 2U);

  // An odd digit count is left padded with a zero.
  TEST_ASSERT_EQ_UINT(byte_util_hex_decode("ABC", 3U, out, sizeof(out), &len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(len, 2U);
  TEST_ASSERT_EQ_MEM(out, expected_odd, 2U);

  // Single digit.
  TEST_ASSERT_EQ_UINT(byte_util_hex_decode("3", 1U, out, sizeof(out), &len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(len, 1U);
  TEST_ASSERT_EQ_UINT(out[0], 0x03U);

  // Rejections.
  TEST_ASSERT_EQ_UINT(byte_util_hex_decode("", 0U, out, sizeof(out), &len),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(byte_util_hex_decode("0x", 2U, out, sizeof(out), &len),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(byte_util_hex_decode("12G4", 4U, out, sizeof(out), &len),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(byte_util_hex_decode("0011223344", 10U, out, sizeof(out), &len),
                      SL_STATUS_WOULD_OVERFLOW);
  TEST_ASSERT_EQ_UINT(byte_util_hex_decode(NULL, 4U, out, sizeof(out), &len),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(byte_util_hex_decode("FFFF", 4U, NULL, sizeof(out), &len),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(byte_util_hex_decode("FFFF", 4U, out, sizeof(out), NULL),
                      SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * Scalar hexadecimal decoding, including the widths the module reports.
 ******************************************************************************/
static void test_hex_to_scalar(void)
{
  uint32_t v32 = 0U;
  uint64_t v64 = 0U;

  // SH, the upper half of the 64-bit address, manual lines 5301 to 5311.
  TEST_ASSERT_EQ_UINT(byte_util_hex_to_u32("13A200", 6U, &v32), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(v32, 0x13A200U);

  TEST_ASSERT_EQ_UINT(byte_util_hex_to_u32("0x3", 3U, &v32), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(v32, 3U);

  // Leading zeroes do not count towards the width, so a fully padded value
  // still decodes.
  TEST_ASSERT_EQ_UINT(byte_util_hex_to_u32("00000000FFFF", 12U, &v32), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(v32, 0xFFFFU);

  TEST_ASSERT_EQ_UINT(byte_util_hex_to_u32("FFFFFFFF", 8U, &v32), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(v32, 0xFFFFFFFFU);
  TEST_ASSERT_EQ_UINT(byte_util_hex_to_u32("1FFFFFFFF", 9U, &v32),
                      SL_STATUS_WOULD_OVERFLOW);

  TEST_ASSERT_EQ_UINT(byte_util_hex_to_u64("0013A20012345678", 16U, &v64),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(v64, 0x0013A20012345678ULL);
  TEST_ASSERT_EQ_UINT(byte_util_hex_to_u64("FFFFFFFFFFFFFFFFF", 17U, &v64),
                      SL_STATUS_WOULD_OVERFLOW);

  TEST_ASSERT_EQ_UINT(byte_util_hex_to_u64("zz", 2U, &v64), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(byte_util_hex_to_u64("", 0U, &v64), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(byte_util_hex_to_u64(NULL, 2U, &v64), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(byte_util_hex_to_u64("12", 2U, NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(byte_util_hex_to_u32("12", 2U, NULL), SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * Variable-width big-endian byte arrays, as API mode returns AT values.
 ******************************************************************************/
static void test_be_to_u64(void)
{
  const uint8_t one[1] = { 0x03U };
  const uint8_t two[2] = { 0x33U, 0x32U };
  const uint8_t eight[8] = {
    0x00U, 0x13U, 0xA2U, 0x00U, 0x12U, 0x34U, 0x56U, 0x78U
  };
  const uint8_t nine[9] = { 0U };
  uint64_t value = 0xDEADU;

  // AP, a single byte, manual lines 5996 to 6013.
  TEST_ASSERT_EQ_UINT(byte_util_be_to_u64(one, 1U, &value), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value, 3U);

  // ID, the default extended PAN identifier 0x3332, manual lines 4853 to 4862.
  TEST_ASSERT_EQ_UINT(byte_util_be_to_u64(two, 2U, &value), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value, 0x3332U);

  TEST_ASSERT_EQ_UINT(byte_util_be_to_u64(eight, 8U, &value), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value, 0x0013A20012345678ULL);

  // A set response carries no value at all.
  TEST_ASSERT_EQ_UINT(byte_util_be_to_u64(NULL, 0U, &value), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value, 0U);

  TEST_ASSERT_EQ_UINT(byte_util_be_to_u64(nine, 9U, &value), SL_STATUS_INVALID_RANGE);
  TEST_ASSERT_EQ_UINT(byte_util_be_to_u64(eight, 8U, NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(byte_util_be_to_u64(NULL, 4U, &value), SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * Encode and decode are inverses over every byte value.
 ******************************************************************************/
static void test_hex_round_trip(void)
{
  uint8_t in[256];
  uint8_t back[256];
  char text[513];
  uint16_t len = 0U;
  uint16_t i;

  for (i = 0U; i < 256U; i++) {
    in[i] = (uint8_t)i;
  }

  TEST_ASSERT_EQ_UINT(byte_util_hex_encode(in, 256U, text, sizeof(text)), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(byte_util_hex_decode(text, 512U, back, sizeof(back), &len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(len, 256U);
  TEST_ASSERT_EQ_MEM(back, in, 256U);
}

/***************************************************************************//**
 * Entry point.
 ******************************************************************************/
int main(void)
{
  TEST_RUN(test_read_be);
  TEST_RUN(test_write_be);
  TEST_RUN(test_hex_encode);
  TEST_RUN(test_hex_decode);
  TEST_RUN(test_hex_to_scalar);
  TEST_RUN(test_be_to_u64);
  TEST_RUN(test_hex_round_trip);

  return TEST_SUMMARY();
}
