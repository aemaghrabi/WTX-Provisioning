/***************************************************************************//**
 * @file
 * @brief Turning what the operator typed into an AT parameter's wire form.
 ******************************************************************************/

#include <stddef.h>
#include <string.h>

#include "byte_util.h"
#include "cli_value.h"

/***************************************************************************//**
 * Report whether a type is carried as a big-endian integer.
 ******************************************************************************/
static bool type_is_numeric(uint8_t type)
{
  return ((type == XBEE_AT_TYPE_U8)
          || (type == XBEE_AT_TYPE_U16)
          || (type == XBEE_AT_TYPE_U32)
          || (type == XBEE_AT_TYPE_U64)
          || (type == XBEE_AT_TYPE_BITMAP));
}

/***************************************************************************//**
 * Parse hexadecimal text into a big-endian value of exactly @p width bytes.
 *
 * The text is decoded first and then left padded, rather than being read into
 * a 64-bit integer, so that the eight-byte parameters (IA, US, D%) go through
 * the same path as the one-byte ones and nothing is truncated on the way.
 ******************************************************************************/
static sl_status_t parse_padded(const char *text,
                                uint16_t width,
                                uint8_t *out,
                                uint16_t cap,
                                uint16_t *len)
{
  uint8_t decoded[XBEE_AT_VALUE_MAX];
  uint16_t decoded_len = 0U;
  uint16_t first = 0U;
  uint16_t significant;
  uint16_t pad;
  sl_status_t status;

  if (width > cap) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  status = byte_util_hex_decode(text, (uint16_t)strlen(text),
                                decoded, (uint16_t)sizeof(decoded),
                                &decoded_len);
  if (status != SL_STATUS_OK) {
    return status;
  }

  // Ignore leading zero bytes before measuring the value. Writing "000C" to a
  // one-byte parameter is an ordinary thing to type, and it names the same
  // value as "0C"; only significant bytes can overflow the parameter.
  while ((first < decoded_len) && (decoded[first] == 0U)) {
    first++;
  }
  significant = (uint16_t)(decoded_len - first);

  if (significant > width) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  pad = (uint16_t)(width - significant);
  (void)memset(out, 0, (size_t)pad);
  (void)memcpy(&out[pad], &decoded[first], (size_t)significant);
  *len = width;

  return SL_STATUS_OK;
}

sl_status_t cli_value_parse(const xbee_at_entry_t *entry,
                            const char *text,
                            uint8_t *out,
                            uint16_t cap,
                            uint16_t *len)
{
  uint16_t text_len;

  if ((entry == NULL) || (text == NULL) || (out == NULL) || (len == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }

  *len = 0U;
  text_len = (uint16_t)strlen(text);

  if (text_len == 0U) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if ((entry->type == XBEE_AT_TYPE_EXEC)
      || (entry->type == XBEE_AT_TYPE_SUBCOMMAND)) {
    // Executable commands act rather than hold a value, and a subcommand is an
    // interpreter of its own that this console does not drive.
    return SL_STATUS_NOT_SUPPORTED;
  }

  if (type_is_numeric(entry->type)) {
    return parse_padded(text, (uint16_t)entry->max_len, out, cap, len);
  }

  if (entry->type == XBEE_AT_TYPE_STRING) {
    if (text_len > (uint16_t)entry->max_len) {
      return SL_STATUS_WOULD_OVERFLOW;
    }
    if (text_len > cap) {
      return SL_STATUS_WOULD_OVERFLOW;
    }

    (void)memcpy(out, text, (size_t)text_len);
    *len = text_len;

    return SL_STATUS_OK;
  }

  if (entry->type == XBEE_AT_TYPE_BYTES) {
    uint16_t decoded_len = 0U;
    uint16_t room = (cap < (uint16_t)entry->max_len)
                    ? cap : (uint16_t)entry->max_len;
    sl_status_t status = byte_util_hex_decode(text, text_len, out, room,
                                              &decoded_len);

    if (status != SL_STATUS_OK) {
      return status;
    }

    *len = decoded_len;

    return SL_STATUS_OK;
  }

  return SL_STATUS_NOT_SUPPORTED;
}

sl_status_t cli_value_default(const xbee_at_entry_t *entry,
                              uint8_t *out,
                              uint16_t cap,
                              uint16_t *len)
{
  uint16_t width;

  if ((entry == NULL) || (out == NULL) || (len == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }

  *len = 0U;

  if ((entry->flags & XBEE_AT_FLAG_NO_DEFAULT) != 0U) {
    return SL_STATUS_NOT_FOUND;
  }
  if ((entry->type == XBEE_AT_TYPE_EXEC)
      || (entry->type == XBEE_AT_TYPE_SUBCOMMAND)) {
    return SL_STATUS_NOT_SUPPORTED;
  }

  width = (uint16_t)entry->max_len;

  if (type_is_numeric(entry->type)) {
    uint64_t value = entry->default_value;
    uint16_t i;

    if (width > cap) {
      return SL_STATUS_WOULD_OVERFLOW;
    }

    // Big-endian, least significant byte last, whatever the declared width.
    for (i = 0U; i < width; i++) {
      out[width - 1U - i] = (uint8_t)(value & 0xFFU);
      value >>= 8;
    }
    *len = width;

    return SL_STATUS_OK;
  }

  if (entry->type == XBEE_AT_TYPE_STRING) {
    // Every string parameter in the table defaults to a single character, as
    // xbee_at_table_is_default() also assumes.
    if (cap < 1U) {
      return SL_STATUS_WOULD_OVERFLOW;
    }

    out[0] = (uint8_t)(entry->default_value & 0xFFU);
    *len = 1U;

    return SL_STATUS_OK;
  }

  if (entry->type == XBEE_AT_TYPE_BYTES) {
    if (width > cap) {
      return SL_STATUS_WOULD_OVERFLOW;
    }

    (void)memset(out, 0, (size_t)width);
    *len = width;

    return SL_STATUS_OK;
  }

  return SL_STATUS_NOT_SUPPORTED;
}

const char *cli_value_type_name(uint8_t type)
{
  const char *name;

  switch (type) {
    case XBEE_AT_TYPE_EXEC:       name = "action"; break;
    case XBEE_AT_TYPE_U8:         name = "8-bit number"; break;
    case XBEE_AT_TYPE_U16:        name = "16-bit number"; break;
    case XBEE_AT_TYPE_U32:        name = "32-bit number"; break;
    case XBEE_AT_TYPE_U64:        name = "64-bit number"; break;
    case XBEE_AT_TYPE_BITMAP:     name = "bit field"; break;
    case XBEE_AT_TYPE_STRING:     name = "text"; break;
    case XBEE_AT_TYPE_BYTES:      name = "hexadecimal bytes"; break;
    case XBEE_AT_TYPE_SUBCOMMAND: name = "subcommand"; break;
    default:                      name = "unknown"; break;
  }

  return name;
}
