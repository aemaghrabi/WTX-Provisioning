/***************************************************************************//**
 * @file
 * @brief Rendering and selection rules for the AT parameter dump.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include "xbee_dump_format.h"
#include "byte_util.h"

/// Characters needed to hexadecimal-encode the longest value, with terminator.
#define HEX_TEXT_CAP  ((XBEE_AT_VALUE_MAX * 2U) + 1U)

/// Lowest and highest byte the string form is willing to print.
#define PRINTABLE_FIRST  0x20U
#define PRINTABLE_LAST   0x7EU

/// Commands that read like parameters but act when they are asked.
///
/// The derived rules below already remove everything that carries no value
/// (XBEE_AT_TYPE_EXEC, XBEE_AT_FLAG_WRITE_ONLY) and everything whose answer the
/// facade cannot deliver (XBEE_AT_FLAG_MULTI_RESPONSE, XBEE_AT_TYPE_SUBCOMMAND).
/// These two are left over, and both have to be named:
///
/// - DN (Discover Node) is not flagged as multi-response. A request with no
///   parameter answers ERROR, but the manual gives the no-response window as
///   NT * 100 milliseconds (lines 5062 to 5075), which is 2.5 seconds at the
///   default NT and so longer than either transport waits. A reply that late
///   arrives while the next command is in flight and desynchronises the rest of
///   the dump. In Command mode a successful DN also leaves Command mode
///   (lines 5058 to 5060).
/// - CB (Commissioning Pushbutton) has no stored value to read back, its
///   default being N/A, and its parameter 4 is "equivalent to sending an RE
///   (Restore Defaults)" (lines 6357 to 6360). A bare ATCB is expected to
///   answer ERROR and nothing more, but a dump has no reason to find out on a
///   module it is meant to leave untouched.
static const uint16_t action_commands[] = {
  XBEE_AT_DN,
  XBEE_AT_CB,
};

/// Category headings, in the order of xbee_at_category_t.
static const char *const category_names[] = {
  "Networking",                  // XBEE_AT_CAT_NETWORKING
  "Discovery",                   // XBEE_AT_CAT_DISCOVERY
  "Coordinator and end device",  // XBEE_AT_CAT_COORDINATOR
  "802.15.4 addressing",         // XBEE_AT_CAT_ADDRESSING
  "Security",                    // XBEE_AT_CAT_SECURITY
  "Secure Session",              // XBEE_AT_CAT_SECURE_SESSION
  "RF interfacing",              // XBEE_AT_CAT_RF
  "MAC diagnostics",             // XBEE_AT_CAT_MAC_DIAG
  "Sleep settings",              // XBEE_AT_CAT_SLEEP
  "MicroPython",                 // XBEE_AT_CAT_MICROPYTHON
  "File system",                 // XBEE_AT_CAT_FILE_SYSTEM
  "Bluetooth Low Energy",        // XBEE_AT_CAT_BLE
  "API configuration",           // XBEE_AT_CAT_API
  "UART interface",              // XBEE_AT_CAT_UART
  "AT command options",          // XBEE_AT_CAT_CMD_OPTIONS
  "UART pin configuration",      // XBEE_AT_CAT_UART_PINS
  "SPI interface",               // XBEE_AT_CAT_SPI
  "I/O settings",                // XBEE_AT_CAT_IO
  "I/O sampling",                // XBEE_AT_CAT_IO_SAMPLING
  "I/O line passing",            // XBEE_AT_CAT_IO_LINE_PASSING
  "Location",                    // XBEE_AT_CAT_LOCATION
  "Diagnostics",                 // XBEE_AT_CAT_DIAGNOSTICS
  "Memory access",               // XBEE_AT_CAT_MEMORY
  "Custom defaults",             // XBEE_AT_CAT_CUSTOM_DEFAULT
};

_Static_assert((sizeof(category_names) / sizeof(category_names[0]))
               == (size_t)XBEE_AT_CAT_COUNT,
               "category_names must name every xbee_at_category_t");

/***************************************************************************//**
 * Report whether a command acts rather than holds a value.
 ******************************************************************************/
static bool is_action_command(uint16_t id)
{
  uint16_t i;

  for (i = 0U; i < (uint16_t)(sizeof(action_commands)
                              / sizeof(action_commands[0])); i++) {
    if (action_commands[i] == id) {
      return true;
    }
  }

  return false;
}

/***************************************************************************//**
 * Report whether every byte of a value can be written to the log as text.
 *
 * A single byte outside the printable range sends the whole value to the
 * hexadecimal form, which also keeps an embedded zero byte from cutting the
 * text short where it is printed.
 ******************************************************************************/
static bool is_printable(const uint8_t *value, uint16_t len)
{
  uint16_t i;

  for (i = 0U; i < len; i++) {
    if ((value[i] < PRINTABLE_FIRST) || (value[i] > PRINTABLE_LAST)) {
      return false;
    }
  }

  return true;
}

/***************************************************************************//**
 * Render a value as uppercase hexadecimal into a caller-provided buffer.
 *
 * @return @p hex on success, or NULL when the value will not fit, which cannot
 *         happen for a value the facade accepted.
 ******************************************************************************/
static const char *to_hex(const uint8_t *value, uint16_t len, char *hex)
{
  if (byte_util_hex_encode(value, len, hex, HEX_TEXT_CAP) != SL_STATUS_OK) {
    return NULL;
  }

  return hex;
}

bool xbee_dump_is_dumpable(const xbee_at_entry_t *entry)
{
  if (entry == NULL) {
    return false;
  }

  // Carries no value to read: XBEE_AT_TYPE_EXEC and the write-only key.
  if (xbee_at_table_validate_get(entry->id) != SL_STATUS_OK) {
    return false;
  }

  // The facade passes no collecting callback for a multi-response command, so
  // the answer is discarded whatever it costs, and it costs the whole
  // collection window. ND and ED would also occupy the radio to produce it.
  if ((entry->flags & XBEE_AT_FLAG_MULTI_RESPONSE) != 0U) {
    return false;
  }

  // A subcommand interpreter has no value of its own.
  if (entry->type == XBEE_AT_TYPE_SUBCOMMAND) {
    return false;
  }

  return !is_action_command(entry->id);
}

const char *xbee_dump_category_name(uint8_t category)
{
  if ((size_t)category >= (sizeof(category_names)
                           / sizeof(category_names[0]))) {
    return "unknown";
  }

  return category_names[category];
}

const char *xbee_dump_format_value(const xbee_at_entry_t *entry,
                                   const uint8_t *value,
                                   uint16_t len,
                                   char *out,
                                   uint16_t cap)
{
  char hex[HEX_TEXT_CAP];
  uint64_t number = 0U;

  if ((out == NULL) || (cap == 0U)) {
    return "(unprintable)";
  }
  if ((entry == NULL) || (value == NULL) || (len == 0U)) {
    return "(no value)";
  }

  switch (entry->type) {
    case XBEE_AT_TYPE_STRING:
    case XBEE_AT_TYPE_SUBCOMMAND: {
      // The value is not terminated, so it is copied before it is printed.
      char text[XBEE_AT_VALUE_MAX + 1U];

      if (len > XBEE_AT_VALUE_MAX) {
        return "(too long)";
      }
      if (!is_printable(value, len)) {
        if (to_hex(value, len, hex) == NULL) {
          return "(unprintable)";
        }
        (void)snprintf(out, cap, "%s (not printable)", hex);
        return out;
      }

      (void)memcpy(text, value, len);
      text[len] = '\0';
      (void)snprintf(out, cap, "\"%s\"", text);
      return out;
    }

    case XBEE_AT_TYPE_BYTES:
      if (to_hex(value, len, hex) == NULL) {
        return "(unprintable)";
      }
      (void)snprintf(out, cap, "%s", hex);
      return out;

    default:
      break;
  }

  // What is left is an integer or a bit field. Both are decoded big-endian from
  // however many bytes arrived, then printed at the width the command table
  // declares: the Command mode transport strips leading zeros, so the arriving
  // length says nothing about the parameter's width.
  if (byte_util_be_to_u64(value, len, &number) != SL_STATUS_OK) {
    if (to_hex(value, len, hex) == NULL) {
      return "(unprintable)";
    }
    (void)snprintf(out, cap, "%s", hex);
    return out;
  }

  switch (entry->type) {
    case XBEE_AT_TYPE_U8:
      (void)snprintf(out, cap, "%u (0x%02X)",
                     (unsigned)number, (unsigned)number);
      break;

    case XBEE_AT_TYPE_U16:
      (void)snprintf(out, cap, "%u (0x%04X)",
                     (unsigned)number, (unsigned)number);
      break;

    case XBEE_AT_TYPE_U32:
      (void)snprintf(out, cap, "%lu (0x%08lX)",
                     (unsigned long)number, (unsigned long)number);
      break;

    case XBEE_AT_TYPE_U64:
      // newlib-nano's printf has no long long conversion, so a value that does
      // not fit a long is printed as two halves. Decimal is dropped there
      // rather than approximated: the commands this wide hold an address and a
      // packed date, where decimal means nothing anyway.
      if ((number >> 32) == 0U) {
        (void)snprintf(out, cap, "%lu (0x%08lX)",
                       (unsigned long)number, (unsigned long)number);
      } else {
        (void)snprintf(out, cap, "0x%08lX%08lX",
                       (unsigned long)(number >> 32),
                       (unsigned long)(number & 0xFFFFFFFFU));
      }
      break;

    case XBEE_AT_TYPE_BITMAP:
      // A bit field is only ever read as bits, so it gets no decimal form.
      if (entry->max_len <= 1U) {
        (void)snprintf(out, cap, "0x%02X", (unsigned)number);
      } else if (entry->max_len == 2U) {
        (void)snprintf(out, cap, "0x%04X", (unsigned)number);
      } else {
        (void)snprintf(out, cap, "0x%08lX", (unsigned long)number);
      }
      break;

    default:
      // XBEE_AT_TYPE_EXEC has no value, and is never dumped.
      if (to_hex(value, len, hex) == NULL) {
        return "(unprintable)";
      }
      (void)snprintf(out, cap, "%s", hex);
      break;
  }

  return out;
}
