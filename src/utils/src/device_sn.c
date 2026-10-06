/***************************************************************************//**
 * @file
 * @brief Device serial number: format, check digit, ISO week and input parsing.
 ******************************************************************************/

#include <stdbool.h>

#include "device_sn.h"

/// Radix of every digit in a serial number.
#define DEVICE_SN_RADIX            10U

/// Modulus of the ISO 7064 MOD 11-10 hybrid system.
#define DEVICE_SN_MOD_11           11U

/// Offset of the first dash in "YYWW-NNNNN-C".
#define DEVICE_SN_DASH1_POS        4U

/// Offset of the first sequence digit.
#define DEVICE_SN_SEQUENCE_POS     5U

/// Offset of the second dash.
#define DEVICE_SN_DASH2_POS        10U

/// Offset of the check digit.
#define DEVICE_SN_CHECK_POS        11U

/// Offset of the terminator.
#define DEVICE_SN_NUL_POS          12U

/// Length of a __DATE__ string, "Mmm dd yyyy".
#define DEVICE_SN_BUILD_DATE_LEN   11U

/// Months in a year.
#define DEVICE_SN_MONTHS           12U

/// Days in a week.
#define DEVICE_SN_DAYS_PER_WEEK    7U

/// ISO weekday of Thursday, Monday being 1.
#define DEVICE_SN_ISO_THURSDAY     4U

/// ISO weekday of Wednesday, Monday being 1.
#define DEVICE_SN_ISO_WEDNESDAY    3U

/// Weeks in a short ISO year; a long one has one more.
#define DEVICE_SN_WEEKS_SHORT      52U

/// Largest ISO week number.
#define DEVICE_SN_WEEKS_LONG       53U

/// Month abbreviations in __DATE__ order, three characters each.
static const char month_names[DEVICE_SN_MONTHS][3] = {
  { 'J', 'a', 'n' }, { 'F', 'e', 'b' }, { 'M', 'a', 'r' }, { 'A', 'p', 'r' },
  { 'M', 'a', 'y' }, { 'J', 'u', 'n' }, { 'J', 'u', 'l' }, { 'A', 'u', 'g' },
  { 'S', 'e', 'p' }, { 'O', 'c', 't' }, { 'N', 'o', 'v' }, { 'D', 'e', 'c' }
};

/// Days in each month of a common year.
static const uint8_t month_days[DEVICE_SN_MONTHS] = {
  31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
};

/// Sakamoto's month offsets for the day of the week.
static const uint8_t weekday_offsets[DEVICE_SN_MONTHS] = {
  0U, 3U, 2U, 5U, 0U, 3U, 5U, 1U, 4U, 6U, 2U, 4U
};

/***************************************************************************//**
 * Report whether a character is a decimal digit.
 ******************************************************************************/
static bool is_digit(char c)
{
  return ((c >= '0') && (c <= '9'));
}

/***************************************************************************//**
 * Value of a decimal digit character already known to be one.
 ******************************************************************************/
static uint8_t digit_value(char c)
{
  return (uint8_t)(c - '0');
}

/***************************************************************************//**
 * Report whether text is exactly count decimal digits followed by a NUL.
 ******************************************************************************/
static bool is_digit_string(const char *text, size_t count)
{
  size_t i;

  for (i = 0U; i < count; i++) {
    if (!is_digit(text[i])) {
      return false;
    }
  }

  return (text[count] == '\0');
}

/***************************************************************************//**
 * Report whether a year is a Gregorian leap year.
 ******************************************************************************/
static bool is_leap_year(uint16_t year)
{
  return (((year % 4U) == 0U) && ((year % 100U) != 0U)) || ((year % 400U) == 0U);
}

/***************************************************************************//**
 * Days in a month, the month being 1 to 12.
 ******************************************************************************/
static uint8_t days_in_month(uint16_t year, uint8_t month)
{
  uint8_t days = month_days[month - 1U];

  if ((month == 2U) && is_leap_year(year)) {
    days++;
  }

  return days;
}

/***************************************************************************//**
 * ISO weekday of a valid date: Monday is 1, Sunday is 7.
 ******************************************************************************/
static uint8_t iso_weekday(uint16_t year, uint8_t month, uint8_t day)
{
  uint32_t y = (month < 3U) ? ((uint32_t)year - 1U) : (uint32_t)year;
  uint32_t sunday_based = (y + (y / 4U) - (y / 100U) + (y / 400U)
                           + weekday_offsets[month - 1U] + day)
                          % DEVICE_SN_DAYS_PER_WEEK;

  return (sunday_based == 0U) ? (uint8_t)DEVICE_SN_DAYS_PER_WEEK
         : (uint8_t)sunday_based;
}

/***************************************************************************//**
 * Day of the year of a valid date, 1 January being 1.
 ******************************************************************************/
static uint16_t day_of_year(uint16_t year, uint8_t month, uint8_t day)
{
  uint16_t days = day;
  uint8_t m;

  for (m = 1U; m < month; m++) {
    days = (uint16_t)(days + days_in_month(year, m));
  }

  return days;
}

/***************************************************************************//**
 * Weeks in an ISO year: 53 when 1 January is a Thursday, or a Wednesday in a
 * leap year, and 52 otherwise.
 ******************************************************************************/
static uint8_t weeks_in_iso_year(uint16_t year)
{
  uint8_t jan1 = iso_weekday(year, 1U, 1U);

  if ((jan1 == DEVICE_SN_ISO_THURSDAY)
      || (is_leap_year(year) && (jan1 == DEVICE_SN_ISO_WEDNESDAY))) {
    return (uint8_t)DEVICE_SN_WEEKS_LONG;
  }

  return (uint8_t)DEVICE_SN_WEEKS_SHORT;
}

/***************************************************************************//**
 * Report whether YYWW names a week that exists in the ISO year 20YY.
 ******************************************************************************/
static bool yyww_is_real(uint16_t yyww)
{
  uint16_t yy = (uint16_t)(yyww / 100U);
  uint16_t ww = (uint16_t)(yyww % 100U);

  if (yy > (DEVICE_SN_YEAR_MAX - DEVICE_SN_YEAR_MIN)) {
    return false;
  }

  return ((ww >= 1U)
          && (ww <= weeks_in_iso_year((uint16_t)(DEVICE_SN_YEAR_MIN + yy))));
}

/***************************************************************************//**
 * Write a value as exactly count decimal digits, zero padded on the left.
 ******************************************************************************/
static void put_digits(char *dst, uint32_t value, uint8_t count)
{
  uint8_t i = count;

  while (i > 0U) {
    i--;
    dst[i] = (char)('0' + (value % DEVICE_SN_RADIX));
    value /= DEVICE_SN_RADIX;
  }
}

/***************************************************************************//**
 * Compute the ISO 7064 MOD 11-10 check digit.
 ******************************************************************************/
sl_status_t device_sn_check_digit(const char *digits, size_t len, uint8_t *check)
{
  uint32_t product = DEVICE_SN_RADIX;
  size_t i;

  if ((digits == NULL) || (check == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }
  if (len == 0U) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  for (i = 0U; i < len; i++) {
    uint32_t sum;

    if (!is_digit(digits[i])) {
      return SL_STATUS_INVALID_PARAMETER;
    }

    sum = (product + digit_value(digits[i])) % DEVICE_SN_RADIX;
    if (sum == 0U) {
      sum = DEVICE_SN_RADIX;
    }
    product = (2U * sum) % DEVICE_SN_MOD_11;
  }

  *check = (uint8_t)((DEVICE_SN_MOD_11 - product) % DEVICE_SN_RADIX);

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Turn a calendar date into YYWW.
 ******************************************************************************/
sl_status_t device_sn_yyww_from_date(uint16_t year,
                                     uint8_t month,
                                     uint8_t day,
                                     uint16_t *yyww)
{
  uint16_t iso_year = year;
  uint8_t week;
  int32_t raw_week;

  if (yyww == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if ((month < 1U) || (month > DEVICE_SN_MONTHS)) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if ((year < DEVICE_SN_YEAR_MIN) || (year > DEVICE_SN_YEAR_MAX)) {
    return SL_STATUS_INVALID_RANGE;
  }
  if ((day < 1U) || (day > days_in_month(year, month))) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // ISO 8601: the week holding the year's first Thursday is week 1. Shifting
  // the day of the year to that week's Thursday and dividing by 7 gives the
  // week, which can fall into the previous or the next ISO year.
  raw_week = ((int32_t)day_of_year(year, month, day)
              - (int32_t)iso_weekday(year, month, day)
              + 10) / (int32_t)DEVICE_SN_DAYS_PER_WEEK;

  if (raw_week < 1) {
    iso_year = (uint16_t)(year - 1U);
    week = weeks_in_iso_year(iso_year);
  } else if (raw_week > (int32_t)weeks_in_iso_year(year)) {
    iso_year = (uint16_t)(year + 1U);
    week = 1U;
  } else {
    week = (uint8_t)raw_week;
  }

  if ((iso_year < DEVICE_SN_YEAR_MIN) || (iso_year > DEVICE_SN_YEAR_MAX)) {
    return SL_STATUS_INVALID_RANGE;
  }

  *yyww = (uint16_t)(((iso_year - DEVICE_SN_YEAR_MIN) * 100U) + week);

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Turn a __DATE__ string into YYWW.
 ******************************************************************************/
sl_status_t device_sn_yyww_from_build_date(const char *date, uint16_t *yyww)
{
  uint8_t month = 0U;
  uint8_t day;
  uint16_t year = 0U;
  uint8_t i;

  if ((date == NULL) || (yyww == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }

  // Length first, so that nothing below reads past the terminator.
  for (i = 0U; i < DEVICE_SN_BUILD_DATE_LEN; i++) {
    if (date[i] == '\0') {
      return SL_STATUS_INVALID_PARAMETER;
    }
  }
  if (date[DEVICE_SN_BUILD_DATE_LEN] != '\0') {
    return SL_STATUS_INVALID_PARAMETER;
  }

  for (i = 0U; i < DEVICE_SN_MONTHS; i++) {
    if ((date[0] == month_names[i][0])
        && (date[1] == month_names[i][1])
        && (date[2] == month_names[i][2])) {
      month = (uint8_t)(i + 1U);
      break;
    }
  }

  if ((month == 0U)
      || (date[3] != ' ')
      || ((date[4] != ' ') && !is_digit(date[4]))
      || !is_digit(date[5])
      || (date[6] != ' ')
      || !is_digit(date[7]) || !is_digit(date[8])
      || !is_digit(date[9]) || !is_digit(date[10])) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // The C standard pads a day below 10 with a space, not a zero.
  day = digit_value(date[5]);
  if (date[4] != ' ') {
    day = (uint8_t)((digit_value(date[4]) * DEVICE_SN_RADIX) + day);
  }

  for (i = 7U; i < DEVICE_SN_BUILD_DATE_LEN; i++) {
    year = (uint16_t)((year * DEVICE_SN_RADIX) + digit_value(date[i]));
  }

  return device_sn_yyww_from_date(year, month, day, yyww);
}

/***************************************************************************//**
 * Parse the NNNNN part.
 ******************************************************************************/
sl_status_t device_sn_parse_sequence(const char *text, uint32_t *sequence)
{
  uint32_t value = 0U;
  uint8_t i;

  if ((text == NULL) || (sequence == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }
  if (!is_digit_string(text, DEVICE_SN_SEQUENCE_DIGITS)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  for (i = 0U; i < DEVICE_SN_SEQUENCE_DIGITS; i++) {
    value = (value * DEVICE_SN_RADIX) + digit_value(text[i]);
  }

  *sequence = value;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Parse a YYWW given as text.
 ******************************************************************************/
sl_status_t device_sn_parse_yyww(const char *text, uint16_t *yyww)
{
  uint16_t value = 0U;
  uint8_t i;

  if ((text == NULL) || (yyww == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }
  if (!is_digit_string(text, DEVICE_SN_YYWW_DIGITS)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  for (i = 0U; i < DEVICE_SN_YYWW_DIGITS; i++) {
    value = (uint16_t)((value * DEVICE_SN_RADIX) + digit_value(text[i]));
  }

  if (!yyww_is_real(value)) {
    return SL_STATUS_INVALID_RANGE;
  }

  *yyww = value;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Build the serial number string.
 ******************************************************************************/
sl_status_t device_sn_format(uint16_t yyww, uint32_t sequence, char *out, size_t cap)
{
  char payload[DEVICE_SN_PAYLOAD_DIGITS];
  uint8_t check = 0U;
  uint8_t i;
  sl_status_t status;

  if (out == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (cap < DEVICE_SN_STR_LEN) {
    return SL_STATUS_WOULD_OVERFLOW;
  }
  if (!yyww_is_real(yyww) || (sequence > DEVICE_SN_SEQUENCE_MAX)) {
    return SL_STATUS_INVALID_RANGE;
  }

  put_digits(payload, yyww, (uint8_t)DEVICE_SN_YYWW_DIGITS);
  put_digits(&payload[DEVICE_SN_YYWW_DIGITS], sequence,
             (uint8_t)DEVICE_SN_SEQUENCE_DIGITS);

  status = device_sn_check_digit(payload, sizeof(payload), &check);
  if (status != SL_STATUS_OK) {
    return status;
  }

  for (i = 0U; i < DEVICE_SN_YYWW_DIGITS; i++) {
    out[i] = payload[i];
  }
  out[DEVICE_SN_DASH1_POS] = '-';
  for (i = 0U; i < DEVICE_SN_SEQUENCE_DIGITS; i++) {
    out[DEVICE_SN_SEQUENCE_POS + i] = payload[DEVICE_SN_YYWW_DIGITS + i];
  }
  out[DEVICE_SN_DASH2_POS] = '-';
  out[DEVICE_SN_CHECK_POS] = (char)('0' + check);
  out[DEVICE_SN_NUL_POS] = '\0';

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Check a stored serial number.
 ******************************************************************************/
sl_status_t device_sn_validate(const char *sn)
{
  char payload[DEVICE_SN_PAYLOAD_DIGITS];
  uint8_t check = 0U;
  uint8_t i;

  if (sn == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  for (i = 0U; i < DEVICE_SN_STR_LEN; i++) {
    bool ok;

    if ((i == DEVICE_SN_DASH1_POS) || (i == DEVICE_SN_DASH2_POS)) {
      ok = (sn[i] == '-');
    } else if (i == DEVICE_SN_NUL_POS) {
      ok = (sn[i] == '\0');
    } else {
      ok = is_digit(sn[i]);
    }

    if (!ok) {
      return SL_STATUS_INVALID_PARAMETER;
    }
  }

  for (i = 0U; i < DEVICE_SN_YYWW_DIGITS; i++) {
    payload[i] = sn[i];
  }
  for (i = 0U; i < DEVICE_SN_SEQUENCE_DIGITS; i++) {
    payload[DEVICE_SN_YYWW_DIGITS + i] = sn[DEVICE_SN_SEQUENCE_POS + i];
  }

  // Cannot fail: every payload character was checked to be a digit above.
  (void)device_sn_check_digit(payload, sizeof(payload), &check);

  if (digit_value(sn[DEVICE_SN_CHECK_POS]) != check) {
    return SL_STATUS_INVALID_SIGNATURE;
  }

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Make stored serial number bytes safe to print.
 ******************************************************************************/
sl_status_t device_sn_to_printable(const char *sn, char *out, size_t cap)
{
  uint8_t i;

  if ((sn == NULL) || (out == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }
  if (cap < DEVICE_SN_STR_LEN) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  for (i = 0U; i < DEVICE_SN_TEXT_LEN; i++) {
    unsigned char c = (unsigned char)sn[i];

    if (c == 0U) {
      break;
    }
    out[i] = ((c >= 0x20U) && (c <= 0x7EU)) ? (char)c : '.';
  }
  out[i] = '\0';

  return SL_STATUS_OK;
}
