/***************************************************************************//**
 * @file
 * @brief Big-endian field accessors and ASCII hexadecimal helpers.
 ******************************************************************************/

#include <stdbool.h>

#include "byte_util.h"

/// Number of hexadecimal digits in one byte.
#define BYTE_UTIL_DIGITS_PER_BYTE  2U

/// Bits shifted per hexadecimal digit.
#define BYTE_UTIL_BITS_PER_DIGIT   4U

/// Characters an abbreviated value ends with, excluding the terminator.
#define BYTE_UTIL_ELLIPSIS_LEN     3U

/// Uppercase digit table.
static const char hex_digits[16] = {
  '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

/***************************************************************************//**
 * Convert one hexadecimal character to its value.
 *
 * @param[in]  c     Character to convert.
 * @param[out] digit Converted value, 0 to 15.
 *
 * @return true if c is a hexadecimal digit, false otherwise.
 ******************************************************************************/
static bool hex_digit_value(char c, uint8_t *digit)
{
  if ((c >= '0') && (c <= '9')) {
    *digit = (uint8_t)(c - '0');
  } else if ((c >= 'a') && (c <= 'f')) {
    *digit = (uint8_t)((c - 'a') + 10);
  } else if ((c >= 'A') && (c <= 'F')) {
    *digit = (uint8_t)((c - 'A') + 10);
  } else {
    return false;
  }

  return true;
}

/***************************************************************************//**
 * Skip an optional "0x" or "0X" prefix.
 *
 * @param[in,out] in     Text pointer, advanced past the prefix.
 * @param[in,out] in_len Remaining length, reduced by the prefix.
 ******************************************************************************/
static void skip_hex_prefix(const char **in, uint16_t *in_len)
{
  if ((*in_len >= 2U)
      && ((*in)[0] == '0')
      && (((*in)[1] == 'x') || ((*in)[1] == 'X'))) {
    *in += 2;
    *in_len = (uint16_t)(*in_len - 2U);
  }
}

/***************************************************************************//**
 * Read a 16-bit big-endian value.
 ******************************************************************************/
uint16_t byte_util_read_be16(const uint8_t *src)
{
  return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

/***************************************************************************//**
 * Read a 32-bit big-endian value.
 ******************************************************************************/
uint32_t byte_util_read_be32(const uint8_t *src)
{
  return ((uint32_t)src[0] << 24)
         | ((uint32_t)src[1] << 16)
         | ((uint32_t)src[2] << 8)
         | (uint32_t)src[3];
}

/***************************************************************************//**
 * Read a 64-bit big-endian value.
 ******************************************************************************/
uint64_t byte_util_read_be64(const uint8_t *src)
{
  return ((uint64_t)byte_util_read_be32(src) << 32)
         | (uint64_t)byte_util_read_be32(&src[4]);
}

/***************************************************************************//**
 * Write a 16-bit big-endian value.
 ******************************************************************************/
void byte_util_write_be16(uint8_t *dst, uint16_t value)
{
  dst[0] = (uint8_t)(value >> 8);
  dst[1] = (uint8_t)value;
}

/***************************************************************************//**
 * Write a 32-bit big-endian value.
 ******************************************************************************/
void byte_util_write_be32(uint8_t *dst, uint32_t value)
{
  dst[0] = (uint8_t)(value >> 24);
  dst[1] = (uint8_t)(value >> 16);
  dst[2] = (uint8_t)(value >> 8);
  dst[3] = (uint8_t)value;
}

/***************************************************************************//**
 * Write a 64-bit big-endian value.
 ******************************************************************************/
void byte_util_write_be64(uint8_t *dst, uint64_t value)
{
  byte_util_write_be32(dst, (uint32_t)(value >> 32));
  byte_util_write_be32(&dst[4], (uint32_t)value);
}

/***************************************************************************//**
 * Encode bytes as uppercase ASCII hexadecimal.
 ******************************************************************************/
sl_status_t byte_util_hex_encode(const uint8_t *in,
                                 uint16_t in_len,
                                 char *out,
                                 uint16_t cap)
{
  uint32_t needed;
  uint16_t i;

  if ((out == NULL) || ((in == NULL) && (in_len > 0U))) {
    return SL_STATUS_NULL_POINTER;
  }

  // Widened so that a large in_len cannot wrap the length check.
  needed = ((uint32_t)in_len * BYTE_UTIL_DIGITS_PER_BYTE) + 1U;
  if (needed > (uint32_t)cap) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  for (i = 0U; i < in_len; i++) {
    out[i * BYTE_UTIL_DIGITS_PER_BYTE] = hex_digits[in[i] >> BYTE_UTIL_BITS_PER_DIGIT];
    out[(i * BYTE_UTIL_DIGITS_PER_BYTE) + 1U] = hex_digits[in[i] & 0x0FU];
  }
  out[in_len * BYTE_UTIL_DIGITS_PER_BYTE] = '\0';

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Format bytes as hexadecimal text for a log line, abbreviated when long.
 ******************************************************************************/
const char *byte_util_hex_text(char *out,
                               uint16_t cap,
                               const uint8_t *in,
                               uint16_t len,
                               uint16_t max_bytes)
{
  uint16_t shown = (len > max_bytes) ? max_bytes : len;
  uint32_t needed;

  if ((in == NULL) || (len == 0U)) {
    return "(empty)";
  }
  if (out == NULL) {
    return "(unprintable)";
  }

  // Widened so that a large length cannot wrap the capacity check.
  needed = ((uint32_t)shown * BYTE_UTIL_DIGITS_PER_BYTE) + 1U;
  if (shown < len) {
    needed += BYTE_UTIL_ELLIPSIS_LEN;
  }
  if (needed > (uint32_t)cap) {
    return "(unprintable)";
  }

  if (byte_util_hex_encode(in, shown, out, cap) != SL_STATUS_OK) {
    return "(unprintable)";
  }

  if (shown < len) {
    // The capacity was checked with the ellipsis included, so this fits.
    uint16_t at = (uint16_t)(shown * BYTE_UTIL_DIGITS_PER_BYTE);

    out[at] = '.';
    out[at + 1U] = '.';
    out[at + 2U] = '.';
    out[at + 3U] = '\0';
  }

  return out;
}

/***************************************************************************//**
 * Decode ASCII hexadecimal text into bytes.
 ******************************************************************************/
sl_status_t byte_util_hex_decode(const char *in,
                                 uint16_t in_len,
                                 uint8_t *out,
                                 uint16_t cap,
                                 uint16_t *out_len)
{
  uint16_t digits;
  uint16_t bytes;
  uint16_t i;
  uint16_t first_width;

  if ((in == NULL) || (out == NULL) || (out_len == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }

  skip_hex_prefix(&in, &in_len);
  digits = in_len;
  if (digits == 0U) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // An odd digit count is treated as left padded with a zero, so the first
  // output byte takes one digit instead of two.
  bytes = (uint16_t)((digits + 1U) / BYTE_UTIL_DIGITS_PER_BYTE);
  if (bytes > cap) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  first_width = (uint16_t)(((digits % BYTE_UTIL_DIGITS_PER_BYTE) == 0U)
                           ? BYTE_UTIL_DIGITS_PER_BYTE : 1U);

  for (i = 0U; i < bytes; i++) {
    uint16_t width = (i == 0U) ? first_width : BYTE_UTIL_DIGITS_PER_BYTE;
    uint16_t offset = (i == 0U)
                      ? 0U
                      : (uint16_t)(first_width
                                   + ((i - 1U) * BYTE_UTIL_DIGITS_PER_BYTE));
    uint8_t value = 0U;
    uint16_t d;

    for (d = 0U; d < width; d++) {
      uint8_t digit;

      if (!hex_digit_value(in[offset + d], &digit)) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      value = (uint8_t)((value << BYTE_UTIL_BITS_PER_DIGIT) | digit);
    }
    out[i] = value;
  }

  *out_len = bytes;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Decode ASCII hexadecimal text into a 64-bit value.
 ******************************************************************************/
sl_status_t byte_util_hex_to_u64(const char *in, uint16_t in_len, uint64_t *value)
{
  uint64_t result = 0U;
  uint16_t significant = 0U;
  uint16_t i;

  if ((in == NULL) || (value == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }

  skip_hex_prefix(&in, &in_len);
  if (in_len == 0U) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  for (i = 0U; i < in_len; i++) {
    uint8_t digit;

    if (!hex_digit_value(in[i], &digit)) {
      return SL_STATUS_INVALID_PARAMETER;
    }
    // Leading zeroes do not count towards the width, so a value padded out to
    // its full field width still decodes.
    if ((significant > 0U) || (digit != 0U)) {
      significant++;
      if (significant > (uint16_t)(sizeof(uint64_t) * BYTE_UTIL_DIGITS_PER_BYTE)) {
        return SL_STATUS_WOULD_OVERFLOW;
      }
      result = (result << BYTE_UTIL_BITS_PER_DIGIT) | digit;
    }
  }

  *value = result;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Decode ASCII hexadecimal text into a 32-bit value.
 ******************************************************************************/
sl_status_t byte_util_hex_to_u32(const char *in, uint16_t in_len, uint32_t *value)
{
  uint64_t wide;
  sl_status_t status;

  if (value == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  status = byte_util_hex_to_u64(in, in_len, &wide);
  if (status != SL_STATUS_OK) {
    return status;
  }
  if (wide > (uint64_t)UINT32_MAX) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  *value = (uint32_t)wide;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Decode a big-endian byte array of at most 8 bytes.
 ******************************************************************************/
sl_status_t byte_util_be_to_u64(const uint8_t *in, uint16_t in_len, uint64_t *value)
{
  uint64_t result = 0U;
  uint16_t i;

  if ((value == NULL) || ((in == NULL) && (in_len > 0U))) {
    return SL_STATUS_NULL_POINTER;
  }
  if (in_len > (uint16_t)sizeof(uint64_t)) {
    return SL_STATUS_INVALID_RANGE;
  }

  for (i = 0U; i < in_len; i++) {
    result = (result << 8) | (uint64_t)in[i];
  }

  *value = result;
  return SL_STATUS_OK;
}
