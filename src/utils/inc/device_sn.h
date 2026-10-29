/***************************************************************************//**
 * @file
 * @brief Device serial number: format, check digit, ISO week and input parsing.
 *
 * The device serial number is the enclosure nameplate number, written once at
 * the factory into the USERDATA flash page (src/services/inc/mfg_store.h) and
 * read by the production firmware (WTX-FW). Its form is
 *
 *     FFYYWWSSSSSC
 *
 * - FF: two-digit product family code. No value is assigned yet, so it is an
 *   input of both the CLI and the provisioning build.
 * - YYWW: last two digits of the ISO 8601 week-numbering year, then the ISO
 *   week (01 to 53). The week-numbering year is used so that the pair is always
 *   a real ISO week: 2027-01-01 lies in week 53 of 2026 and gives 2653.
 * - SSSSS: five-digit sequence number, 00000 to 99999.
 * - C: ISO 7064 MOD 11-10 check digit over the eleven digits FFYYWWSSSSS.
 *
 * Twelve decimal digits, no separators, no letters. It is stored as exactly
 * DEVICE_SN_STORED_LEN (13) bytes: the digits followed by one NUL, which is
 * what WTX-FW reads (its DEVICE_SN_STR_LEN). In RAM it is kept in
 * DEVICE_SN_STR_SIZE bytes so it can be printed.
 *
 * Specification: docs/plan/2026-10-29-serial-number-family-code.md. The
 * specification's example "180726001234" does not satisfy ISO 7064 MOD 11-10;
 * the check digit of 18072600123 is 0.
 *
 * The module is hardware independent and depends only on sl_status.h and the C
 * standard library, so it also builds for the host unit tests in test/.
 ******************************************************************************/

#ifndef DEVICE_SN_H
#define DEVICE_SN_H

#include <stddef.h>
#include <stdint.h>
#include "sl_status.h"

/// Digits of a serial number, without its NUL terminator.
#define DEVICE_SN_LEN              12U

/// Bytes of a stored serial number: the digits followed by one NUL. Must equal
/// DEVICE_SN_STR_LEN in WTX-FW lcd_menu.h, which reads exactly this many.
#define DEVICE_SN_STORED_LEN       (DEVICE_SN_LEN + 1U)

/// Bytes of a RAM buffer holding a serial number as a C string.
#define DEVICE_SN_STR_SIZE         (DEVICE_SN_LEN + 1U)

/// Digits of the FF part.
#define DEVICE_SN_FAMILY_DIGITS    2U

/// Digits of the YYWW part.
#define DEVICE_SN_YYWW_DIGITS      4U

/// Digits of the SSSSS part.
#define DEVICE_SN_SEQUENCE_DIGITS  5U

/// Digits the check digit is computed over: FF, YYWW, then SSSSS.
#define DEVICE_SN_PAYLOAD_DIGITS \
  (DEVICE_SN_FAMILY_DIGITS + DEVICE_SN_YYWW_DIGITS + DEVICE_SN_SEQUENCE_DIGITS)

/// Largest product family code.
#define DEVICE_SN_FAMILY_MAX       99U

/// Largest sequence number.
#define DEVICE_SN_SEQUENCE_MAX     99999UL

/// First calendar year a two-digit YY can stand for.
#define DEVICE_SN_YEAR_MIN         2000U

/// Last calendar year a two-digit YY can stand for.
#define DEVICE_SN_YEAR_MAX         2099U

/***************************************************************************//**
 * Compute the ISO 7064 MOD 11-10 check digit of a string of decimal digits.
 *
 * P starts at 10. For each digit d: S = (P + d) mod 10, with 0 taken as 10,
 * then P = (2 * S) mod 11. The check digit is (11 - P) mod 10. The standard's
 * own example is 0794 -> 5.
 *
 * @param[in]  digits ASCII decimal digits, not required to be NUL terminated.
 * @param[in]  len    Number of digits, at least 1.
 * @param[out] check  Check digit, 0 to 9.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if digits or check is NULL,
 *         SL_STATUS_INVALID_PARAMETER if len is 0 or a character is not a
 *         decimal digit.
 ******************************************************************************/
sl_status_t device_sn_check_digit(const char *digits, size_t len, uint8_t *check);

/***************************************************************************//**
 * Turn a calendar date into YYWW: ISO week-numbering year and ISO week.
 *
 * @param[in]  year  Calendar year, DEVICE_SN_YEAR_MIN to DEVICE_SN_YEAR_MAX.
 * @param[in]  month 1 to 12.
 * @param[in]  day   1 to the length of the month.
 * @param[out] yyww  (ISO year mod 100) * 100 + ISO week.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if yyww is NULL,
 *         SL_STATUS_INVALID_PARAMETER if the date does not exist,
 *         SL_STATUS_INVALID_RANGE if the date, or the ISO year it falls in, is
 *         outside DEVICE_SN_YEAR_MIN to DEVICE_SN_YEAR_MAX.
 ******************************************************************************/
sl_status_t device_sn_yyww_from_date(uint16_t year,
                                     uint8_t month,
                                     uint8_t day,
                                     uint16_t *yyww);

/***************************************************************************//**
 * Turn a compiler build date, as __DATE__ writes it, into YYWW.
 *
 * The form is "Mmm dd yyyy", eleven characters, with a day below 10 padded by a
 * space: "Oct  6 2026".
 *
 * @param[in]  date NUL-terminated build date.
 * @param[out] yyww See device_sn_yyww_from_date().
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if date or yyww is NULL,
 *         SL_STATUS_INVALID_PARAMETER if the text is not in that form or the
 *         date does not exist,
 *         SL_STATUS_INVALID_RANGE as for device_sn_yyww_from_date().
 ******************************************************************************/
sl_status_t device_sn_yyww_from_build_date(const char *date, uint16_t *yyww);

/***************************************************************************//**
 * Parse the FF part: exactly two decimal digits.
 *
 * @param[in]  text   NUL-terminated text.
 * @param[out] family Value, 0 to 99.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if text or family is NULL,
 *         SL_STATUS_INVALID_PARAMETER if text is not exactly two digits.
 ******************************************************************************/
sl_status_t device_sn_parse_family(const char *text, uint8_t *family);

/***************************************************************************//**
 * Parse the SSSSS part: exactly five decimal digits.
 *
 * @param[in]  text     NUL-terminated text.
 * @param[out] sequence Value, 0 to DEVICE_SN_SEQUENCE_MAX.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if text or sequence is NULL,
 *         SL_STATUS_INVALID_PARAMETER if text is not exactly five digits.
 ******************************************************************************/
sl_status_t device_sn_parse_sequence(const char *text, uint32_t *sequence);

/***************************************************************************//**
 * Parse a YYWW given as text: exactly four decimal digits naming a real week.
 *
 * YY stands for the year 20YY. WW must be 01 to 52, or 53 when that ISO year
 * has 53 weeks.
 *
 * @param[in]  text NUL-terminated text.
 * @param[out] yyww Value.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if text or yyww is NULL,
 *         SL_STATUS_INVALID_PARAMETER if text is not exactly four digits,
 *         SL_STATUS_INVALID_RANGE if that ISO year has no such week.
 ******************************************************************************/
sl_status_t device_sn_parse_yyww(const char *text, uint16_t *yyww);

/***************************************************************************//**
 * Build the serial number "FFYYWWSSSSSC", followed by a NUL for printing.
 *
 * Before anything is written to out, the result is checked independently: the
 * check digit is recomputed from the formatted digits and the whole number is
 * passed through device_sn_validate(). The digits and the NUL written to out,
 * DEVICE_SN_STORED_LEN bytes, are exactly the stored form.
 *
 * @param[in]  family   0 to 99.
 * @param[in]  yyww     YYWW, which must name a real ISO week.
 * @param[in]  sequence 0 to DEVICE_SN_SEQUENCE_MAX.
 * @param[out] out      Destination, DEVICE_SN_STR_SIZE bytes are written.
 * @param[in]  cap      Capacity of out in bytes.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if out is NULL,
 *         SL_STATUS_WOULD_OVERFLOW if cap is below DEVICE_SN_STR_SIZE,
 *         SL_STATUS_INVALID_RANGE if family is above 99, yyww is not a real
 *         ISO week or sequence is above DEVICE_SN_SEQUENCE_MAX,
 *         SL_STATUS_FAIL if the self-check rejects the result.
 *         out is left untouched on error.
 ******************************************************************************/
sl_status_t device_sn_format(uint8_t family,
                             uint16_t yyww,
                             uint32_t sequence,
                             char *out,
                             size_t cap);

/***************************************************************************//**
 * Check a stored serial number: its shape, then its check digit.
 *
 * The shape is DEVICE_SN_LEN decimal digits; no terminator is expected or
 * read. Whether YYWW names a real week is not checked, because WTX-FW does not
 * check it either.
 *
 * @param[in] sn DEVICE_SN_LEN readable bytes, as read from USERDATA.
 *
 * @return SL_STATUS_OK if the serial number is well formed and its check digit
 *         is right,
 *         SL_STATUS_NULL_POINTER if sn is NULL,
 *         SL_STATUS_INVALID_PARAMETER if the shape is wrong,
 *         SL_STATUS_INVALID_SIGNATURE if only the check digit is wrong.
 ******************************************************************************/
sl_status_t device_sn_validate(const char *sn);

/***************************************************************************//**
 * Make stored serial number bytes safe to print, whatever they hold.
 *
 * Copies at most DEVICE_SN_LEN characters, stopping at a NUL, replaces every
 * byte outside printable ASCII with '.', and terminates the copy.
 *
 * @param[in]  sn  DEVICE_SN_LEN readable bytes.
 * @param[out] out Destination text.
 * @param[in]  cap Capacity of out in bytes, at least DEVICE_SN_STR_SIZE.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if sn or out is NULL,
 *         SL_STATUS_WOULD_OVERFLOW if cap is below DEVICE_SN_STR_SIZE.
 ******************************************************************************/
sl_status_t device_sn_to_printable(const char *sn, char *out, size_t cap);

#endif  // DEVICE_SN_H
