/***************************************************************************//**
 * @file
 * @brief Unit tests for the device_sn utility.
 *
 * ISO week vectors were taken from Python's datetime.date.isocalendar(). The
 * check digit vectors use the ISO 7064 MOD 11-10 example from the standard
 * (0794 -> 5) and serial numbers this project produces.
 ******************************************************************************/

#include "test_util.h"
#include "device_sn.h"

/// One date and the YYWW it must give.
typedef struct {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint16_t yyww;
} date_case_t;

/***************************************************************************//**
 * The check digit matches the standard and the serial numbers in use.
 ******************************************************************************/
static void test_check_digit(void)
{
  uint8_t check = 0xFFU;

  TEST_ASSERT_EQ_UINT(device_sn_check_digit("0794", 4U, &check), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(check, 5U);

  // GitHub issue #4 shows 2640-00123-4; the standard gives 8.
  TEST_ASSERT_EQ_UINT(device_sn_check_digit("264000123", 9U, &check), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(check, 8U);

  TEST_ASSERT_EQ_UINT(device_sn_check_digit("264100123", 9U, &check), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(check, 3U);

  TEST_ASSERT_EQ_UINT(device_sn_check_digit("265300001", 9U, &check), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(check, 9U);

  TEST_ASSERT_EQ_UINT(device_sn_check_digit("000000000", 9U, &check), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(check, 6U);

  TEST_ASSERT_EQ_UINT(device_sn_check_digit("999999999", 9U, &check), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(check, 1U);
}

/***************************************************************************//**
 * The check digit rejects what is not a string of digits.
 ******************************************************************************/
static void test_check_digit_errors(void)
{
  uint8_t check = 0U;

  TEST_ASSERT_EQ_UINT(device_sn_check_digit(NULL, 4U, &check), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(device_sn_check_digit("0794", 4U, NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(device_sn_check_digit("0794", 0U, &check),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_check_digit("07a4", 4U, &check),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_check_digit("2640-0012", 9U, &check),
                      SL_STATUS_INVALID_PARAMETER);
}

/***************************************************************************//**
 * Dates map to the ISO week-numbering year and week, across year boundaries.
 ******************************************************************************/
static void test_yyww_from_date(void)
{
  static const date_case_t cases[] = {
    { 2026U, 10U, 6U, 2641U },
    { 2026U, 12U, 28U, 2653U },  // 2026 has 53 weeks.
    { 2027U, 1U, 1U, 2653U },    // Still week 53 of 2026.
    { 2024U, 12U, 30U, 2501U },  // Already week 1 of 2025.
    { 2021U, 1U, 3U, 2053U },    // Still week 53 of 2020.
    { 2020U, 12U, 31U, 2053U },
    { 2024U, 2U, 29U, 2409U },   // Leap day.
    { 2010U, 1U, 3U, 953U },     // Week 53 of 2009, written 0953.
    { 2000U, 1U, 3U, 1U },       // First day of ISO year 2000, written 0001.
    { 2099U, 12U, 31U, 9953U },
  };
  size_t i;

  for (i = 0U; i < (sizeof(cases) / sizeof(cases[0])); i++) {
    uint16_t yyww = 0xFFFFU;
    sl_status_t status = device_sn_yyww_from_date(cases[i].year, cases[i].month,
                                                  cases[i].day, &yyww);

    if ((status != SL_STATUS_OK) || (yyww != cases[i].yyww)) {
      TEST_FAIL("%04u-%02u-%02u: expected %04u, got %04u, status 0x%04X",
                (unsigned)cases[i].year, (unsigned)cases[i].month,
                (unsigned)cases[i].day, (unsigned)cases[i].yyww,
                (unsigned)yyww, (unsigned)status);
    }
  }
}

/***************************************************************************//**
 * Dates that do not exist, or that YY cannot express, are refused.
 ******************************************************************************/
static void test_yyww_from_date_errors(void)
{
  uint16_t yyww = 0U;

  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_date(2026U, 10U, 6U, NULL),
                      SL_STATUS_NULL_POINTER);

  // 2000-01-01 falls in ISO year 1999.
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_date(2000U, 1U, 1U, &yyww),
                      SL_STATUS_INVALID_RANGE);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_date(1999U, 12U, 31U, &yyww),
                      SL_STATUS_INVALID_RANGE);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_date(2100U, 1U, 1U, &yyww),
                      SL_STATUS_INVALID_RANGE);

  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_date(2026U, 2U, 29U, &yyww),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_date(2026U, 4U, 31U, &yyww),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_date(2026U, 13U, 1U, &yyww),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_date(2026U, 0U, 1U, &yyww),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_date(2026U, 1U, 0U, &yyww),
                      SL_STATUS_INVALID_PARAMETER);
}

/***************************************************************************//**
 * The compiler's __DATE__ form is understood, space-padded day included.
 ******************************************************************************/
static void test_yyww_from_build_date(void)
{
  uint16_t yyww = 0U;

  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date("Oct  6 2026", &yyww),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(yyww, 2641U);

  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date("Jan  1 2027", &yyww),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(yyww, 2653U);

  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date("Dec 30 2024", &yyww),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(yyww, 2501U);

  // A zero-padded day is not what the standard writes, but means the same.
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date("Oct 06 2026", &yyww),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(yyww, 2641U);

  // The compiler's own date has to parse, whatever day this test is built.
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date(__DATE__, &yyww),
                      SL_STATUS_OK);
}

/***************************************************************************//**
 * Anything other than "Mmm dd yyyy" naming a real date is refused.
 ******************************************************************************/
static void test_yyww_from_build_date_errors(void)
{
  uint16_t yyww = 0U;

  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date(NULL, &yyww),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date("Oct  6 2026", NULL),
                      SL_STATUS_NULL_POINTER);

  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date("", &yyww),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date("Oct 6 2026", &yyww),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date("Oct  6 2026 ", &yyww),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date("oct  6 2026", &yyww),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date("Foo  6 2026", &yyww),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date("Oct  6 20x6", &yyww),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date("Feb 30 2026", &yyww),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_yyww_from_build_date("Jan  1 1999", &yyww),
                      SL_STATUS_INVALID_RANGE);
}

/***************************************************************************//**
 * The sequence number is exactly five digits.
 ******************************************************************************/
static void test_parse_sequence(void)
{
  uint32_t sequence = 0xFFFFFFFFU;

  TEST_ASSERT_EQ_UINT(device_sn_parse_sequence("00123", &sequence), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(sequence, 123U);
  TEST_ASSERT_EQ_UINT(device_sn_parse_sequence("00000", &sequence), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(sequence, 0U);
  TEST_ASSERT_EQ_UINT(device_sn_parse_sequence("99999", &sequence), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(sequence, 99999U);
  // Would be the octal constant 0 89 in C, and rejected there; here it is text.
  TEST_ASSERT_EQ_UINT(device_sn_parse_sequence("00089", &sequence), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(sequence, 89U);

  TEST_ASSERT_EQ_UINT(device_sn_parse_sequence(NULL, &sequence), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(device_sn_parse_sequence("00123", NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(device_sn_parse_sequence("", &sequence),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_parse_sequence("1234", &sequence),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_parse_sequence("123456", &sequence),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_parse_sequence("12a45", &sequence),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_parse_sequence(" 0123", &sequence),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_parse_sequence("-0123", &sequence),
                      SL_STATUS_INVALID_PARAMETER);
}

/***************************************************************************//**
 * YYWW text is four digits naming a week that exists.
 ******************************************************************************/
static void test_parse_yyww(void)
{
  uint16_t yyww = 0U;

  TEST_ASSERT_EQ_UINT(device_sn_parse_yyww("2641", &yyww), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(yyww, 2641U);
  TEST_ASSERT_EQ_UINT(device_sn_parse_yyww("2653", &yyww), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(yyww, 2653U);
  TEST_ASSERT_EQ_UINT(device_sn_parse_yyww("2053", &yyww), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(device_sn_parse_yyww("0001", &yyww), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(yyww, 1U);

  // 2027 has 52 weeks; no year has a week 0 or 54.
  TEST_ASSERT_EQ_UINT(device_sn_parse_yyww("2753", &yyww), SL_STATUS_INVALID_RANGE);
  TEST_ASSERT_EQ_UINT(device_sn_parse_yyww("2600", &yyww), SL_STATUS_INVALID_RANGE);
  TEST_ASSERT_EQ_UINT(device_sn_parse_yyww("2654", &yyww), SL_STATUS_INVALID_RANGE);

  TEST_ASSERT_EQ_UINT(device_sn_parse_yyww(NULL, &yyww), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(device_sn_parse_yyww("2641", NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(device_sn_parse_yyww("264", &yyww), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_parse_yyww("26411", &yyww), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_parse_yyww("26a1", &yyww), SL_STATUS_INVALID_PARAMETER);
}

/***************************************************************************//**
 * The serial number is exactly the 13 bytes WTX-FW expects.
 ******************************************************************************/
static void test_format(void)
{
  static const char expected[DEVICE_SN_STR_LEN] = {
    '2', '6', '4', '1', '-', '0', '0', '1', '2', '3', '-', '3', '\0'
  };
  char sn[DEVICE_SN_STR_LEN + 1U];

  (void)memset(sn, 'X', sizeof(sn));
  TEST_ASSERT_EQ_UINT(device_sn_format(2641U, 123U, sn, DEVICE_SN_STR_LEN),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_MEM(sn, expected, DEVICE_SN_STR_LEN);
  // Nothing written past the 13 bytes.
  TEST_ASSERT_EQ_UINT((uint8_t)sn[DEVICE_SN_STR_LEN], (uint8_t)'X');

  TEST_ASSERT_EQ_UINT(device_sn_format(2640U, 123U, sn, sizeof(sn)), SL_STATUS_OK);
  TEST_ASSERT_EQ_STR(sn, "2640-00123-8");

  TEST_ASSERT_EQ_UINT(device_sn_format(2653U, 99999U, sn, sizeof(sn)), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(device_sn_validate(sn), SL_STATUS_OK);

  TEST_ASSERT_EQ_UINT(device_sn_format(1U, 0U, sn, sizeof(sn)), SL_STATUS_OK);
  TEST_ASSERT_EQ_STR(sn, "0001-00000-8");
  TEST_ASSERT_EQ_UINT(device_sn_validate(sn), SL_STATUS_OK);
}

/***************************************************************************//**
 * Bad inputs to the formatter leave the output untouched.
 ******************************************************************************/
static void test_format_errors(void)
{
  char sn[DEVICE_SN_STR_LEN];

  (void)memset(sn, 'X', sizeof(sn));

  TEST_ASSERT_EQ_UINT(device_sn_format(2641U, 123U, NULL, sizeof(sn)),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(device_sn_format(2641U, 123U, sn, DEVICE_SN_STR_LEN - 1U),
                      SL_STATUS_WOULD_OVERFLOW);
  TEST_ASSERT_EQ_UINT(device_sn_format(2641U, 100000U, sn, sizeof(sn)),
                      SL_STATUS_INVALID_RANGE);
  TEST_ASSERT_EQ_UINT(device_sn_format(2654U, 123U, sn, sizeof(sn)),
                      SL_STATUS_INVALID_RANGE);
  TEST_ASSERT_EQ_UINT(device_sn_format(2753U, 123U, sn, sizeof(sn)),
                      SL_STATUS_INVALID_RANGE);
  TEST_ASSERT_EQ_UINT(device_sn_format(2600U, 123U, sn, sizeof(sn)),
                      SL_STATUS_INVALID_RANGE);
  TEST_ASSERT_EQ_UINT(device_sn_format(10001U, 123U, sn, sizeof(sn)),
                      SL_STATUS_INVALID_RANGE);
  TEST_ASSERT_EQ_UINT((uint8_t)sn[0], (uint8_t)'X');
}

/***************************************************************************//**
 * Stored values are checked for shape first, then for the check digit.
 ******************************************************************************/
static void test_validate(void)
{
  static const char no_nul[DEVICE_SN_STR_LEN] = {
    '2', '6', '4', '1', '-', '0', '0', '1', '2', '3', '-', '3', '3'
  };

  TEST_ASSERT_EQ_UINT(device_sn_validate("2641-00123-3"), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(device_sn_validate("2640-00123-8"), SL_STATUS_OK);

  TEST_ASSERT_EQ_UINT(device_sn_validate("2641-00123-4"), SL_STATUS_INVALID_SIGNATURE);
  // The example in GitHub issue #4.
  TEST_ASSERT_EQ_UINT(device_sn_validate("2640-00123-4"), SL_STATUS_INVALID_SIGNATURE);

  TEST_ASSERT_EQ_UINT(device_sn_validate(NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(device_sn_validate("2641_00123-3"), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_validate("2641-00123_3"), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_validate("26A1-00123-3"), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_validate("2641-00123-X"), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(device_sn_validate(no_nul), SL_STATUS_INVALID_PARAMETER);
}

/***************************************************************************//**
 * Whatever bytes are stored, the printable copy is bounded text.
 ******************************************************************************/
static void test_to_printable(void)
{
  static const char garbage[DEVICE_SN_STR_LEN] = {
    '2', '6', '\x01', '1', '-', '\xFF', '0', '1', '2', '3', '-', '3', '9'
  };
  static const char short_value[DEVICE_SN_STR_LEN] = {
    '2', '6', '4', '1', '\0', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'
  };
  char out[DEVICE_SN_STR_LEN];

  TEST_ASSERT_EQ_UINT(device_sn_to_printable("2641-00123-3", out, sizeof(out)),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_STR(out, "2641-00123-3");

  // Non-printable bytes become dots, and the 13th byte is never copied.
  TEST_ASSERT_EQ_UINT(device_sn_to_printable(garbage, out, sizeof(out)), SL_STATUS_OK);
  TEST_ASSERT_EQ_STR(out, "26.1-.0123-3");

  TEST_ASSERT_EQ_UINT(device_sn_to_printable(short_value, out, sizeof(out)),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_STR(out, "2641");

  TEST_ASSERT_EQ_UINT(device_sn_to_printable(NULL, out, sizeof(out)),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(device_sn_to_printable(garbage, NULL, sizeof(out)),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(device_sn_to_printable(garbage, out, DEVICE_SN_STR_LEN - 1U),
                      SL_STATUS_WOULD_OVERFLOW);
}

int main(void)
{
  TEST_RUN(test_check_digit);
  TEST_RUN(test_check_digit_errors);
  TEST_RUN(test_yyww_from_date);
  TEST_RUN(test_yyww_from_date_errors);
  TEST_RUN(test_yyww_from_build_date);
  TEST_RUN(test_yyww_from_build_date_errors);
  TEST_RUN(test_parse_sequence);
  TEST_RUN(test_parse_yyww);
  TEST_RUN(test_format);
  TEST_RUN(test_format_errors);
  TEST_RUN(test_validate);
  TEST_RUN(test_to_printable);
  return TEST_SUMMARY();
}
