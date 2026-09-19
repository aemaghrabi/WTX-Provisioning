/***************************************************************************//**
 * @file
 * @brief Unit tests for the console's value parsing.
 *
 * Every case runs against a real entry of the command table, so the widths,
 * ranges and defaults exercised here are the ones the firmware will use.
 ******************************************************************************/

#include <string.h>

#include "test_util.h"
#include "cli_value.h"
#include "xbee_at_table.h"

/// Scratch for a parsed value.
static uint8_t value[XBEE_AT_VALUE_MAX];

/// Length written by the last parse.
static uint16_t value_len;

/***************************************************************************//**
 * Look up a command, failing the test when it is missing from the table.
 ******************************************************************************/
static const xbee_at_entry_t *entry_of(uint16_t id)
{
  const xbee_at_entry_t *entry = xbee_at_table_find(id);

  if (entry == NULL) {
    TEST_FAIL("command 0x%04X is not in the table", (unsigned)id);
  }

  return entry;
}

/***************************************************************************//**
 * Parse text for a command and return the status.
 ******************************************************************************/
static sl_status_t parse(uint16_t id, const char *text)
{
  const xbee_at_entry_t *entry = entry_of(id);

  value_len = 0U;
  (void)memset(value, 0xEE, sizeof(value));

  return cli_value_parse(entry, text, value, (uint16_t)sizeof(value),
                         &value_len);
}

/***************************************************************************//**
 * A one-byte parameter is written at its declared width.
 *
 * CH is U8, range 0x0B to 0x1A.
 ******************************************************************************/
static void test_parse_u8(void)
{
  static const uint8_t expect[] = { 0x0CU };

  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "0C"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 1U);
  TEST_ASSERT_EQ_MEM(value, expect, sizeof(expect));

  // A single digit names the same value.
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "C"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 1U);
  TEST_ASSERT_EQ_MEM(value, expect, sizeof(expect));

  // The module's own "0x" convention is accepted.
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "0x0C"), SL_STATUS_OK);
  TEST_ASSERT_EQ_MEM(value, expect, sizeof(expect));
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "0X0c"), SL_STATUS_OK);
  TEST_ASSERT_EQ_MEM(value, expect, sizeof(expect));
}

/***************************************************************************//**
 * A wider parameter is left padded to its declared width.
 *
 * ID is U16, so the module gets both bytes even when one was typed. Writing
 * the full width is what keeps a Command mode write and an API write identical.
 ******************************************************************************/
static void test_parse_pads_to_width(void)
{
  static const uint8_t expect_id[] = { 0x00U, 0x0FU };
  static const uint8_t expect_dh[] = { 0x00U, 0x00U, 0x00U, 0x01U };

  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_ID_, "F"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 2U);
  TEST_ASSERT_EQ_MEM(value, expect_id, sizeof(expect_id));

  // DH is U32.
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_DH, "1"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 4U);
  TEST_ASSERT_EQ_MEM(value, expect_dh, sizeof(expect_dh));
}

/***************************************************************************//**
 * Leading zero bytes do not count against the width.
 ******************************************************************************/
static void test_parse_leading_zeros(void)
{
  static const uint8_t expect[] = { 0x0CU };

  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "000C"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 1U);
  TEST_ASSERT_EQ_MEM(value, expect, sizeof(expect));

  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "0x00000C"), SL_STATUS_OK);
  TEST_ASSERT_EQ_MEM(value, expect, sizeof(expect));

  // All zeros is the value zero, at the full width.
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_ID_, "0000"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 2U);
  TEST_ASSERT_EQ_UINT(value[0], 0U);
  TEST_ASSERT_EQ_UINT(value[1], 0U);
}

/***************************************************************************//**
 * An eight-byte parameter survives intact.
 *
 * IA is U64 and defaults to all ones, so a 32-bit intermediate would lose the
 * top half of anything typed.
 ******************************************************************************/
static void test_parse_u64(void)
{
  static const uint8_t expect[] = {
    0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xABU, 0xCDU, 0xEFU
  };

  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_IA, "0123456789ABCDEF"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 8U);
  TEST_ASSERT_EQ_MEM(value, expect, sizeof(expect));
}

/***************************************************************************//**
 * A value with more significant bytes than the parameter holds is refused.
 ******************************************************************************/
static void test_parse_too_wide(void)
{
  // CH holds one byte.
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "1FF"), SL_STATUS_WOULD_OVERFLOW);
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "0100"), SL_STATUS_WOULD_OVERFLOW);
  // ID holds two.
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_ID_, "123456"), SL_STATUS_WOULD_OVERFLOW);
}

/***************************************************************************//**
 * Text that is not hexadecimal, or not there at all, is refused.
 ******************************************************************************/
static void test_parse_bad_text(void)
{
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, ""), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "zz"), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "0C "), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "-1"), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "0x"), SL_STATUS_INVALID_PARAMETER);
}

/***************************************************************************//**
 * A string parameter is taken exactly as typed.
 *
 * NI is a 20-character string, and must not be read as hexadecimal even when
 * it happens to look like it.
 ******************************************************************************/
static void test_parse_string(void)
{
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_NI, "sensor-1"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 8U);
  TEST_ASSERT_EQ_MEM(value, "sensor-1", 8U);

  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_NI, "ABCD"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 4U);
  TEST_ASSERT_EQ_MEM(value, "ABCD", 4U);

  // 20 characters fit, 21 do not.
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_NI, "12345678901234567890"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 20U);
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_NI, "123456789012345678901"),
                      SL_STATUS_WOULD_OVERFLOW);
}

/***************************************************************************//**
 * A byte parameter keeps the width it was given.
 *
 * KY is a 16-byte key, unlike an integer it is not padded.
 ******************************************************************************/
static void test_parse_bytes(void)
{
  static const uint8_t expect[] = {
    0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
    0x88U, 0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU
  };

  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_KY, "00112233445566778899AABBCCDDEEFF"),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 16U);
  TEST_ASSERT_EQ_MEM(value, expect, sizeof(expect));

  // Longer than the parameter holds.
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_KY,
                            "00112233445566778899AABBCCDDEEFF00"),
                      SL_STATUS_WOULD_OVERFLOW);
}

/***************************************************************************//**
 * A command that acts rather than holds a value is refused.
 ******************************************************************************/
static void test_parse_valueless(void)
{
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_WR, "1"), SL_STATUS_NOT_SUPPORTED);
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_AC, "0"), SL_STATUS_NOT_SUPPORTED);
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_FS, "ls"), SL_STATUS_NOT_SUPPORTED);
}

/***************************************************************************//**
 * A parsed value satisfies the table's own range check, or is caught by it.
 *
 * This is the join between the two: the console parses, then hands the result
 * to xbee_at_table_validate_set() for the documented bounds.
 ******************************************************************************/
static void test_parse_then_validate(void)
{
  // CH is documented as 0x0B to 0x1A.
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "0C"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_CH, value, value_len),
                      SL_STATUS_OK);

  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "FF"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_CH, value, value_len),
                      SL_STATUS_INVALID_RANGE);

  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_CH, "0A"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_CH, value, value_len),
                      SL_STATUS_INVALID_RANGE);

  // A read-only parameter is refused by the table, not by the parser.
  TEST_ASSERT_EQ_UINT(parse(XBEE_AT_SH, "1"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_SH, value, value_len),
                      SL_STATUS_PERMISSION);
}

/***************************************************************************//**
 * The factory default is built in the same wire form a write uses.
 ******************************************************************************/
static void test_default(void)
{
  const xbee_at_entry_t *entry;

  // CH defaults to 0x0C.
  entry = entry_of(XBEE_AT_CH);
  TEST_ASSERT_EQ_UINT(cli_value_default(entry, value, (uint16_t)sizeof(value),
                                        &value_len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 1U);
  TEST_ASSERT_EQ_UINT(value[0], 0x0CU);
  TEST_ASSERT(xbee_at_table_is_default(entry, value, value_len));

  // ID defaults to 0x3332, across two bytes.
  entry = entry_of(XBEE_AT_ID_);
  TEST_ASSERT_EQ_UINT(cli_value_default(entry, value, (uint16_t)sizeof(value),
                                        &value_len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 2U);
  TEST_ASSERT_EQ_UINT(value[0], 0x33U);
  TEST_ASSERT_EQ_UINT(value[1], 0x32U);
  TEST_ASSERT(xbee_at_table_is_default(entry, value, value_len));

  // IA defaults to all ones, which only an eight-byte path can express.
  entry = entry_of(XBEE_AT_IA);
  TEST_ASSERT_EQ_UINT(cli_value_default(entry, value, (uint16_t)sizeof(value),
                                        &value_len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 8U);
  TEST_ASSERT(xbee_at_table_is_default(entry, value, value_len));

  // NI defaults to a single space.
  entry = entry_of(XBEE_AT_NI);
  TEST_ASSERT_EQ_UINT(cli_value_default(entry, value, (uint16_t)sizeof(value),
                                        &value_len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(value_len, 1U);
  TEST_ASSERT_EQ_UINT(value[0], (uint8_t)' ');
  TEST_ASSERT(xbee_at_table_is_default(entry, value, value_len));
}

/***************************************************************************//**
 * Every provisionable command has a default the console can write back.
 *
 * This is what makes "no xbee at <NAME>" safe to offer for the whole table
 * rather than for a hand-picked list.
 ******************************************************************************/
static void test_default_covers_the_table(void)
{
  uint16_t i;

  for (i = 0U; i < xbee_at_table_count(); i++) {
    const xbee_at_entry_t *entry = xbee_at_table_at(i);
    sl_status_t status;

    if ((entry == NULL) || !xbee_at_table_is_provisionable(entry)) {
      continue;
    }

    status = cli_value_default(entry, value, (uint16_t)sizeof(value),
                               &value_len);
    if (status != SL_STATUS_OK) {
      TEST_FAIL("entry %u has no usable default, status 0x%04X",
                (unsigned)i, (unsigned)status);
      continue;
    }

    if (!xbee_at_table_is_default(entry, value, value_len)) {
      TEST_FAIL("entry %u default does not read back as the default",
                (unsigned)i);
    }

    status = xbee_at_table_validate_set(entry->id, value, value_len);
    if (status != SL_STATUS_OK) {
      TEST_FAIL("entry %u default is rejected by validate_set, status 0x%04X",
                (unsigned)i, (unsigned)status);
    }
  }
}

/***************************************************************************//**
 * A command with no documented default says so.
 ******************************************************************************/
static void test_default_absent(void)
{
  // AI is read only and carries no documented default.
  TEST_ASSERT_EQ_UINT(cli_value_default(entry_of(XBEE_AT_AI), value,
                                        (uint16_t)sizeof(value), &value_len),
                      SL_STATUS_NOT_FOUND);

  // WR executes; there is nothing to default.
  TEST_ASSERT_EQ_UINT(cli_value_default(entry_of(XBEE_AT_WR), value,
                                        (uint16_t)sizeof(value), &value_len),
                      SL_STATUS_NOT_FOUND);
}

/***************************************************************************//**
 * A destination smaller than the value is refused, not overrun.
 ******************************************************************************/
static void test_small_destination(void)
{
  const xbee_at_entry_t *entry = entry_of(XBEE_AT_DH);  // U32.
  uint8_t small[2];
  uint16_t len = 0U;

  TEST_ASSERT_EQ_UINT(cli_value_parse(entry, "1", small,
                                      (uint16_t)sizeof(small), &len),
                      SL_STATUS_WOULD_OVERFLOW);
  TEST_ASSERT_EQ_UINT(cli_value_default(entry, small,
                                        (uint16_t)sizeof(small), &len),
                      SL_STATUS_WOULD_OVERFLOW);
}

/***************************************************************************//**
 * Type names are available for every type, for the error messages.
 ******************************************************************************/
static void test_type_names(void)
{
  TEST_ASSERT_EQ_STR(cli_value_type_name(XBEE_AT_TYPE_U8), "8-bit number");
  TEST_ASSERT_EQ_STR(cli_value_type_name(XBEE_AT_TYPE_STRING), "text");
  TEST_ASSERT_EQ_STR(cli_value_type_name(XBEE_AT_TYPE_EXEC), "action");
  TEST_ASSERT_EQ_STR(cli_value_type_name(200U), "unknown");
}

/***************************************************************************//**
 * Both entry points reject NULL arguments.
 ******************************************************************************/
static void test_null_arguments(void)
{
  const xbee_at_entry_t *entry = entry_of(XBEE_AT_CH);
  uint16_t len = 0U;

  TEST_ASSERT_EQ_UINT(cli_value_parse(NULL, "0C", value, 1U, &len),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(cli_value_parse(entry, NULL, value, 1U, &len),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(cli_value_parse(entry, "0C", NULL, 1U, &len),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(cli_value_parse(entry, "0C", value, 1U, NULL),
                      SL_STATUS_NULL_POINTER);

  TEST_ASSERT_EQ_UINT(cli_value_default(NULL, value, 1U, &len),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(cli_value_default(entry, NULL, 1U, &len),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(cli_value_default(entry, value, 1U, NULL),
                      SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * Entry point.
 ******************************************************************************/
int main(void)
{
  TEST_RUN(test_parse_u8);
  TEST_RUN(test_parse_pads_to_width);
  TEST_RUN(test_parse_leading_zeros);
  TEST_RUN(test_parse_u64);
  TEST_RUN(test_parse_too_wide);
  TEST_RUN(test_parse_bad_text);
  TEST_RUN(test_parse_string);
  TEST_RUN(test_parse_bytes);
  TEST_RUN(test_parse_valueless);
  TEST_RUN(test_parse_then_validate);
  TEST_RUN(test_default);
  TEST_RUN(test_default_covers_the_table);
  TEST_RUN(test_default_absent);
  TEST_RUN(test_small_destination);
  TEST_RUN(test_type_names);
  TEST_RUN(test_null_arguments);

  return TEST_SUMMARY();
}
