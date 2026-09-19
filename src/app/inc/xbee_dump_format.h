/***************************************************************************//**
 * @file
 * @brief Rendering and selection rules for the AT parameter dump.
 *
 * Decides which AT commands the dump may ask the module for, and turns a value
 * that came back into readable text using the type the command table records.
 * Keeping both here rather than in xbee_dump.c is what makes them testable:
 * this module touches no hardware and no driver state.
 *
 * The selection rule is application policy, not a property of the command set,
 * which is why it lives in the application layer rather than beside
 * xbee_at_table_is_provisionable(). It answers "what is safe to ask a module
 * for at boot on a fixture", and the transports and the provisioning
 * application have no business inheriting that judgement.
 *
 * @note This module is hardware independent. It depends only on the command
 *       table, byte_util, sl_status.h and the C standard library, so it also
 *       builds for the host unit tests.
 ******************************************************************************/

#ifndef XBEE_DUMP_FORMAT_H
#define XBEE_DUMP_FORMAT_H

#include <stdbool.h>
#include <stdint.h>
#include "xbee_at_table.h"

/// Characters needed for the longest rendering of a value.
///
/// The longest value the facade will hand over is XBEE_AT_VALUE_MAX bytes,
/// which is 130 hexadecimal characters, and the widest decoration around it is
/// the unprintable string form, " (not printable)", at 16 characters plus the
/// terminator. Nothing is ever truncated at this size, so the renderer needs no
/// abbreviation rule.
#define XBEE_DUMP_VALUE_TEXT_CAP  160U

/***************************************************************************//**
 * Report whether the dump may read this command from the module.
 *
 * False for the commands that carry no readable value, and for the ones that
 * look like parameters but act when asked: a discovery that occupies the radio,
 * a subcommand interpreter, or a pushbutton simulator whose parameter 4 is a
 * restore to defaults. See the table in the implementation for the reason
 * against each one.
 *
 * @param[in] entry Command table entry, may be NULL.
 *
 * @return true when the command may be read, false otherwise.
 ******************************************************************************/
bool xbee_dump_is_dumpable(const xbee_at_entry_t *entry);

/***************************************************************************//**
 * The name of a command category, for the heading above its parameters.
 *
 * @param[in] category An xbee_at_category_t value, or anything else.
 *
 * @return The name, or "unknown" when the category is out of range. Never NULL
 *         and never changes.
 ******************************************************************************/
const char *xbee_dump_category_name(uint8_t category);

/***************************************************************************//**
 * Render a parameter value as text, decoded by the command's type.
 *
 * Integers are decoded big-endian from however many bytes arrived and printed
 * at the width the command table declares, not at the width that arrived: the
 * Command mode transport strips leading zeros, so the same parameter reaches
 * this function as one byte over Command mode and as two over API. Keying off
 * the entry rather than the length is what makes a dump taken in one mode
 * comparable with a dump taken in the other.
 *
 * The caller provides the buffer rather than this returning a shared one, so
 * that two values can be rendered in one log statement and so that the function
 * stays free of state.
 *
 * @param[in]  entry Command table entry, may be NULL.
 * @param[in]  value Value bytes, may be NULL when len is 0.
 * @param[in]  len   Value length.
 * @param[out] out   Destination, at least XBEE_DUMP_VALUE_TEXT_CAP characters.
 * @param[in]  cap   Capacity of out, including the terminator.
 *
 * @return @p out, or a literal when there is nothing to render or the
 *         destination is unusable. Never NULL.
 ******************************************************************************/
const char *xbee_dump_format_value(const xbee_at_entry_t *entry,
                                   const uint8_t *value,
                                   uint16_t len,
                                   char *out,
                                   uint16_t cap);

#endif  // XBEE_DUMP_FORMAT_H
