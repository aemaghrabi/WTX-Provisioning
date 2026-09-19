/***************************************************************************//**
 * @file
 * @brief Big-endian field accessors and ASCII hexadecimal helpers.
 *
 * XBee API frames carry every multi-byte field big-endian
 * (docs/manuals/xbee_90002273_ref_manual.md, line 7266), and Command mode
 * exchanges the same values as ASCII hexadecimal text
 * (same manual, lines 3094 to 3096). These helpers cover both representations.
 *
 * The module is hardware independent and depends only on sl_status.h and the C
 * standard library, so it also builds for the host unit tests in test/.
 ******************************************************************************/

#ifndef BYTE_UTIL_H
#define BYTE_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include "sl_status.h"

/***************************************************************************//**
 * Read a 16-bit big-endian value.
 *
 * @param[in] src Two readable bytes.
 *
 * @return Decoded value.
 ******************************************************************************/
uint16_t byte_util_read_be16(const uint8_t *src);

/***************************************************************************//**
 * Read a 32-bit big-endian value.
 *
 * @param[in] src Four readable bytes.
 *
 * @return Decoded value.
 ******************************************************************************/
uint32_t byte_util_read_be32(const uint8_t *src);

/***************************************************************************//**
 * Read a 64-bit big-endian value.
 *
 * @param[in] src Eight readable bytes.
 *
 * @return Decoded value.
 ******************************************************************************/
uint64_t byte_util_read_be64(const uint8_t *src);

/***************************************************************************//**
 * Write a 16-bit big-endian value.
 *
 * @param[out] dst   Two writable bytes.
 * @param[in]  value Value to store.
 ******************************************************************************/
void byte_util_write_be16(uint8_t *dst, uint16_t value);

/***************************************************************************//**
 * Write a 32-bit big-endian value.
 *
 * @param[out] dst   Four writable bytes.
 * @param[in]  value Value to store.
 ******************************************************************************/
void byte_util_write_be32(uint8_t *dst, uint32_t value);

/***************************************************************************//**
 * Write a 64-bit big-endian value.
 *
 * @param[out] dst   Eight writable bytes.
 * @param[in]  value Value to store.
 ******************************************************************************/
void byte_util_write_be64(uint8_t *dst, uint64_t value);

/***************************************************************************//**
 * Encode bytes as uppercase ASCII hexadecimal, without a 0x prefix.
 *
 * The output is always NUL terminated, so @p cap must be at least
 * 2 * in_len + 1.
 *
 * @param[in]  in     Source bytes, may be NULL only when in_len is 0.
 * @param[in]  in_len Number of source bytes.
 * @param[out] out    Destination text buffer.
 * @param[in]  cap    Capacity of out in bytes, including the terminator.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if out is NULL, or in is NULL with in_len > 0,
 *         SL_STATUS_WOULD_OVERFLOW if cap is too small. out is left untouched.
 ******************************************************************************/
sl_status_t byte_util_hex_encode(const uint8_t *in,
                                 uint16_t in_len,
                                 char *out,
                                 uint16_t cap);

/***************************************************************************//**
 * Format bytes as hexadecimal text for a log line, abbreviated when long.
 *
 * Unlike byte_util_hex_encode() this never fails: every outcome is a string the
 * caller can print. The caller provides the buffer rather than this returning a
 * shared one, so that two values can appear side by side in one call.
 *
 * At most @p max_bytes bytes are encoded; an ellipsis follows when the value is
 * longer. The buffer therefore has to hold 2 * max_bytes + 4 characters for the
 * longest result.
 *
 * @param[out] out       Destination text buffer, may be NULL.
 * @param[in]  cap       Capacity of out in bytes, including the terminator.
 * @param[in]  in        Source bytes, may be NULL when len is 0.
 * @param[in]  len       Number of source bytes.
 * @param[in]  max_bytes Bytes to encode before abbreviating.
 *
 * @return @p out, or the literal "(empty)" when there is nothing to print, or
 *         the literal "(unprintable)" when out is NULL or too small. Never
 *         NULL.
 ******************************************************************************/
const char *byte_util_hex_text(char *out,
                               uint16_t cap,
                               const uint8_t *in,
                               uint16_t len,
                               uint16_t max_bytes);

/***************************************************************************//**
 * Decode ASCII hexadecimal text into bytes.
 *
 * An optional "0x" or "0X" prefix is accepted and skipped. An odd number of
 * digits is treated as if the value were left padded with a zero, so "ABC"
 * decodes to 0x0A 0xBC. This matches the module, which accepts hexadecimal
 * parameters with or without a leading 0x (manual lines 3094 to 3096).
 *
 * @param[in]  in     Source text, not required to be NUL terminated.
 * @param[in]  in_len Number of source characters to consider.
 * @param[out] out    Destination bytes.
 * @param[in]  cap    Capacity of out in bytes.
 * @param[out] out_len Number of bytes written.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if in, out or out_len is NULL,
 *         SL_STATUS_INVALID_PARAMETER if the text holds a non-hexadecimal
 *         character or no digits at all,
 *         SL_STATUS_WOULD_OVERFLOW if the value needs more than cap bytes.
 ******************************************************************************/
sl_status_t byte_util_hex_decode(const char *in,
                                 uint16_t in_len,
                                 uint8_t *out,
                                 uint16_t cap,
                                 uint16_t *out_len);

/***************************************************************************//**
 * Decode ASCII hexadecimal text into a 32-bit value.
 *
 * @param[in]  in     Source text.
 * @param[in]  in_len Number of source characters.
 * @param[out] value  Decoded value.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if in or value is NULL,
 *         SL_STATUS_INVALID_PARAMETER on a non-hexadecimal character or an
 *         empty value,
 *         SL_STATUS_WOULD_OVERFLOW if more than 8 significant digits are given.
 ******************************************************************************/
sl_status_t byte_util_hex_to_u32(const char *in, uint16_t in_len, uint32_t *value);

/***************************************************************************//**
 * Decode ASCII hexadecimal text into a 64-bit value.
 *
 * @param[in]  in     Source text.
 * @param[in]  in_len Number of source characters.
 * @param[out] value  Decoded value.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if in or value is NULL,
 *         SL_STATUS_INVALID_PARAMETER on a non-hexadecimal character or an
 *         empty value,
 *         SL_STATUS_WOULD_OVERFLOW if more than 16 significant digits are given.
 ******************************************************************************/
sl_status_t byte_util_hex_to_u64(const char *in, uint16_t in_len, uint64_t *value);

/***************************************************************************//**
 * Decode a big-endian byte array of at most 8 bytes into a 64-bit value.
 *
 * API mode returns AT parameter values as raw big-endian bytes whose width
 * depends on the command, so the caller does not always know it in advance.
 *
 * @param[in]  in     Source bytes, may be NULL only when in_len is 0.
 * @param[in]  in_len Number of source bytes, 0 to 8. Zero decodes to 0.
 * @param[out] value  Decoded value.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if value is NULL, or in is NULL with in_len > 0,
 *         SL_STATUS_INVALID_RANGE if in_len is above 8.
 ******************************************************************************/
sl_status_t byte_util_be_to_u64(const uint8_t *in, uint16_t in_len, uint64_t *value);

#endif  // BYTE_UTIL_H
