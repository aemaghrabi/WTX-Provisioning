/***************************************************************************//**
 * @file
 * @brief Turning what the operator typed into an AT parameter's wire form.
 *
 * Private to the console. Kept apart from cli.c because it is the part most
 * worth testing: everything here is decided by the command table entry, so one
 * set of cases covers all 147 commands, and a mistake would put a wrong value
 * into a module's flash rather than merely print something odd.
 *
 * Pure: no hardware, no input or output, no state.
 ******************************************************************************/

#ifndef CLI_VALUE_H
#define CLI_VALUE_H

#include <stdint.h>
#include "sl_status.h"
#include "xbee_at_table.h"

/***************************************************************************//**
 * Parse a typed value into the bytes the module expects.
 *
 * The command's type decides how the text is read:
 * - Integers and bit fields are hexadecimal, with or without a "0x" prefix,
 *   which is the module's own convention (manual lines 3094 to 3096). The
 *   result is written big-endian at the full width the table declares, so that
 *   "F" written to a two-byte parameter becomes 0x00 0x0F.
 * - A string is taken exactly as typed.
 * - A byte parameter is hexadecimal and keeps the width it was given.
 * - A command that executes, or a text subcommand, carries no value and is
 *   refused.
 *
 * The value's range is not checked here. Pass the result to
 * xbee_at_table_validate_set(), which owns the documented bounds.
 *
 * @param[in]  entry Command table entry.
 * @param[in]  text  What the operator typed, NUL-terminated.
 * @param[out] out   Destination for the wire form.
 * @param[in]  cap   Capacity of out in bytes.
 * @param[out] len   Number of bytes written.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if any argument is NULL,
 *         SL_STATUS_NOT_SUPPORTED if the command takes no value,
 *         SL_STATUS_INVALID_PARAMETER if the text is empty or not hexadecimal
 *             where hexadecimal is required,
 *         SL_STATUS_WOULD_OVERFLOW if the value is wider than the command
 *             allows, or wider than cap.
 ******************************************************************************/
sl_status_t cli_value_parse(const xbee_at_entry_t *entry,
                            const char *text,
                            uint8_t *out,
                            uint16_t cap,
                            uint16_t *len);

/***************************************************************************//**
 * Build the command's documented factory default in wire form.
 *
 * Mirrors how xbee_at_table_is_default() reads the table: an integer is the
 * stored default written big-endian at the declared width, a string is its one
 * default character, and a byte parameter is all zeros.
 *
 * @param[in]  entry Command table entry.
 * @param[out] out   Destination for the wire form.
 * @param[in]  cap   Capacity of out in bytes.
 * @param[out] len   Number of bytes written.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if any argument is NULL,
 *         SL_STATUS_NOT_FOUND if the manual gives no default for the command,
 *         SL_STATUS_NOT_SUPPORTED if the command takes no value,
 *         SL_STATUS_WOULD_OVERFLOW if cap is too small.
 ******************************************************************************/
sl_status_t cli_value_default(const xbee_at_entry_t *entry,
                              uint8_t *out,
                              uint16_t cap,
                              uint16_t *len);

/***************************************************************************//**
 * The name of a command's type, for an error message.
 *
 * @param[in] type An xbee_at_type_t value, or anything else.
 *
 * @return The name, or "unknown". Never NULL and never changes.
 ******************************************************************************/
const char *cli_value_type_name(uint8_t type);

#endif  // CLI_VALUE_H
