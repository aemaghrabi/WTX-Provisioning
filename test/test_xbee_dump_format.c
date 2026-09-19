/***************************************************************************//**
 * @file
 * @brief Unit tests for the AT parameter dump's selection and rendering rules.
 *
 * The dump reads the whole command table from a live module, so the two rules
 * that decide what it asks for and how it prints the answer are the parts worth
 * proving away from hardware: a mistake in the first one makes a read-only
 * application act on the module, and a mistake in the second one makes a dump
 * taken over Command mode disagree with the same module dumped over API.
 ******************************************************************************/

#include <string.h>

#include "test_util.h"
#include "xbee_at_table.h"
#include "xbee_dump_format.h"

/// Parameters the dump is expected to print, as counted by the skip rule.
///
/// A change to this number means the command table gained or lost a readable
/// parameter, or that the skip rule changed. Either is worth noticing, which is
/// why it is asserted rather than derived.
#define EXPECTED_DUMPABLE_COUNT  126U

/***************************************************************************//**
 * Render a value with a buffer of the ordinary size.
 ******************************************************************************/
static const char *render(uint16_t command, const uint8_t *value, uint16_t len,
                          char *out)
{
  return xbee_dump_format_value(xbee_at_table_find(command), value, len,
                                out, XBEE_DUMP_VALUE_TEXT_CAP);
}

/***************************************************************************//**
 * Every category has a heading, and nothing else does.
 ******************************************************************************/
static void test_category_names(void)
{
  uint8_t i;

  for (i = 0U; i < (uint8_t)XBEE_AT_CAT_COUNT; i++) {
    const char *name = xbee_dump_category_name(i);

    TEST_ASSERT(name != NULL);
    TEST_ASSERT(name[0] != '\0');
    TEST_ASSERT(strcmp(name, "unknown") != 0);
  }

  // Out of range must not index past the array.
  TEST_ASSERT_EQ_STR(xbee_dump_category_name((uint8_t)XBEE_AT_CAT_COUNT),
                     "unknown");
  TEST_ASSERT_EQ_STR(xbee_dump_category_name(0xFFU), "unknown");
}

/***************************************************************************//**
 * The skip rule keeps the dump away from everything that acts when asked.
 ******************************************************************************/
static void test_skip_rule(void)
{
  // Commands that carry no readable value.
  static const uint16_t no_value[] = {
    XBEE_AT_AS, XBEE_AT_DA, XBEE_AT_FP, XBEE_AT_CN, XBEE_AT_IS,
    XBEE_AT_PCT_P, XBEE_AT_FR, XBEE_AT_AC, XBEE_AT_WR, XBEE_AT_RE,
    XBEE_AT_PCT_F, XBEE_AT_BANG_C, XBEE_AT_R1,
    XBEE_AT_KY,  // Write-only: reading it answers a status, not the key.
  };
  // Commands that would act, scan the air or run a subcommand if they were
  // asked for their value.
  static const uint16_t action_like[] = {
    XBEE_AT_ND, XBEE_AT_DN, XBEE_AT_ED, XBEE_AT_VL, XBEE_AT_FS,
    XBEE_AT_PY, XBEE_AT_CB,
  };
  // A sample of the ordinary parameters a dump exists to report, one per value
  // type that survives the rule.
  static const uint16_t dumpable[] = {
    XBEE_AT_CH,     // U8
    XBEE_AT_ID_,    // U16
    XBEE_AT_SH,     // U32, read-only
    XBEE_AT_D_PCT,  // U64, read-only
    XBEE_AT_NI,     // STRING
    XBEE_AT_FK,     // BYTES, the longest value in the set
    XBEE_AT_P5,     // Not fitted on every variant, but still worth asking for
  };
  uint16_t count = 0U;
  uint16_t i;

  for (i = 0U; i < (uint16_t)(sizeof(no_value) / sizeof(no_value[0])); i++) {
    if (xbee_dump_is_dumpable(xbee_at_table_find(no_value[i]))) {
      TEST_FAIL("command 0x%04X has no readable value but is dumpable",
                (unsigned)no_value[i]);
    }
  }

  for (i = 0U; i < (uint16_t)(sizeof(action_like) / sizeof(action_like[0]));
       i++) {
    if (xbee_dump_is_dumpable(xbee_at_table_find(action_like[i]))) {
      TEST_FAIL("command 0x%04X acts when asked but is dumpable",
                (unsigned)action_like[i]);
    }
  }

  for (i = 0U; i < (uint16_t)(sizeof(dumpable) / sizeof(dumpable[0])); i++) {
    if (!xbee_dump_is_dumpable(xbee_at_table_find(dumpable[i]))) {
      TEST_FAIL("command 0x%04X is an ordinary parameter but is skipped",
                (unsigned)dumpable[i]);
    }
  }

  // A missing entry is not dumpable, and must not crash the walk.
  TEST_ASSERT(!xbee_dump_is_dumpable(NULL));
  TEST_ASSERT(!xbee_dump_is_dumpable(xbee_at_table_find(XBEE_AT_ID('Z', 'Z'))));

  // Nothing the rule admits may be one of the commands that cannot answer.
  for (i = 0U; i < xbee_at_table_count(); i++) {
    const xbee_at_entry_t *entry = xbee_at_table_at(i);

    if (!xbee_dump_is_dumpable(entry)) {
      continue;
    }

    count++;
    if (xbee_at_table_validate_get(entry->id) != SL_STATUS_OK) {
      TEST_FAIL("command 0x%04X is dumpable but cannot be read",
                (unsigned)entry->id);
    }
    if ((entry->flags & XBEE_AT_FLAG_MULTI_RESPONSE) != 0U) {
      TEST_FAIL("command 0x%04X is dumpable but answers on several lines",
                (unsigned)entry->id);
    }
  }

  TEST_ASSERT_EQ_UINT(count, EXPECTED_DUMPABLE_COUNT);
}

/***************************************************************************//**
 * Integers print in decimal and hexadecimal, at the command's declared width.
 ******************************************************************************/
static void test_format_integers(void)
{
  static const uint8_t ch[] = { 0x0C };
  static const uint8_t id[] = { 0x33, 0x32 };
  static const uint8_t sh[] = { 0x00, 0x13, 0xA2, 0x00 };
  char out[XBEE_DUMP_VALUE_TEXT_CAP];

  TEST_ASSERT_EQ_STR(render(XBEE_AT_CH, ch, sizeof(ch), out), "12 (0x0C)");
  TEST_ASSERT_EQ_STR(render(XBEE_AT_ID_, id, sizeof(id), out),
                     "13106 (0x3332)");
  TEST_ASSERT_EQ_STR(render(XBEE_AT_SH, sh, sizeof(sh), out),
                     "1286656 (0x0013A200)");
}

/***************************************************************************//**
 * A width is taken from the command table, never from the reply.
 *
 * The Command mode transport strips leading zeros, so the same parameter
 * arrives as one byte there and as two over API. Both must render alike, or a
 * dump could not be compared against one taken in the other mode.
 ******************************************************************************/
static void test_format_width_comes_from_the_table(void)
{
  static const uint8_t short_form[] = { 0x64 };
  static const uint8_t long_form[] = { 0x00, 0x64 };
  char out[XBEE_DUMP_VALUE_TEXT_CAP];
  char expected[XBEE_DUMP_VALUE_TEXT_CAP];

  // ID is a U16: one arriving byte must still print four hexadecimal digits.
  (void)strcpy(expected, render(XBEE_AT_ID_, long_form, sizeof(long_form), out));
  TEST_ASSERT_EQ_STR(expected, "100 (0x0064)");
  TEST_ASSERT_EQ_STR(render(XBEE_AT_ID_, short_form, sizeof(short_form), out),
                     expected);
}

/***************************************************************************//**
 * A 64-bit value prints as two halves once it will not fit a long.
 ******************************************************************************/
static void test_format_u64(void)
{
  static const uint8_t all_ff[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  static const uint8_t small[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x34 };
  char out[XBEE_DUMP_VALUE_TEXT_CAP];

  TEST_ASSERT_EQ_STR(render(XBEE_AT_IA, all_ff, sizeof(all_ff), out),
                     "0xFFFFFFFFFFFFFFFF");
  // Below 2^32 there is a decimal form worth printing.
  TEST_ASSERT_EQ_STR(render(XBEE_AT_IA, small, sizeof(small), out),
                     "4660 (0x00001234)");
}

/***************************************************************************//**
 * A bit field prints as bits only, at its declared width.
 ******************************************************************************/
static void test_format_bitmap(void)
{
  static const uint8_t one[] = { 0x03 };
  static const uint8_t two[] = { 0xFF, 0xFF };
  char out[XBEE_DUMP_VALUE_TEXT_CAP];

  // C8 (Compatibility Options) is a one-byte bit field.
  TEST_ASSERT_EQ_STR(render(XBEE_AT_C8, one, sizeof(one), out), "0x03");
  // SC (Scan Channels) is two bytes wide.
  TEST_ASSERT_EQ_STR(render(XBEE_AT_SC, two, sizeof(two), out), "0xFFFF");
}

/***************************************************************************//**
 * Text prints as text, and anything that is not text prints as bytes.
 ******************************************************************************/
static void test_format_strings_and_bytes(void)
{
  static const uint8_t text[] = { 's', 'e', 'n', 's', 'o', 'r', '-', '1' };
  static const uint8_t binary[] = { 'a', 0x01, 'b' };
  static const uint8_t embedded_nul[] = { 'a', 0x00, 'b' };
  static const uint8_t bytes[] = { 0xDE, 0xAD, 0xBE, 0xEF };
  char out[XBEE_DUMP_VALUE_TEXT_CAP];

  TEST_ASSERT_EQ_STR(render(XBEE_AT_NI, text, sizeof(text), out),
                     "\"sensor-1\"");

  // A control byte sends the whole value to the hexadecimal form rather than
  // letting it reach the log as an escape sequence.
  TEST_ASSERT_EQ_STR(render(XBEE_AT_NI, binary, sizeof(binary), out),
                     "610162 (not printable)");

  // An embedded zero must not cut the printed text short.
  TEST_ASSERT_EQ_STR(render(XBEE_AT_NI, embedded_nul, sizeof(embedded_nul), out),
                     "610062 (not printable)");

  // A byte parameter is never guessed at as text.
  TEST_ASSERT_EQ_STR(render(XBEE_AT_KY, bytes, sizeof(bytes), out), "DEADBEEF");
}

/***************************************************************************//**
 * A value that is absent, or a request that cannot be rendered, says so.
 ******************************************************************************/
static void test_format_edge_cases(void)
{
  static const uint8_t value[] = { 0x0C };
  char out[XBEE_DUMP_VALUE_TEXT_CAP];

  TEST_ASSERT_EQ_STR(render(XBEE_AT_CH, value, 0U, out), "(no value)");
  TEST_ASSERT_EQ_STR(render(XBEE_AT_CH, NULL, 0U, out), "(no value)");
  TEST_ASSERT_EQ_STR(render(XBEE_AT_ID('Z', 'Z'), value, sizeof(value), out),
                     "(no value)");
  TEST_ASSERT_EQ_STR(xbee_dump_format_value(xbee_at_table_find(XBEE_AT_CH),
                                            value, sizeof(value), NULL,
                                            XBEE_DUMP_VALUE_TEXT_CAP),
                     "(unprintable)");
  TEST_ASSERT_EQ_STR(xbee_dump_format_value(xbee_at_table_find(XBEE_AT_CH),
                                            value, sizeof(value), out, 0U),
                     "(unprintable)");
}

/***************************************************************************//**
 * A destination too small to hold the rendering is not written past.
 ******************************************************************************/
static void test_format_respects_capacity(void)
{
  static const uint8_t sh[] = { 0x00, 0x13, 0xA2, 0x00 };
  char guarded[XBEE_DUMP_VALUE_TEXT_CAP];
  const uint16_t cap = 4U;
  uint16_t i;

  (void)memset(guarded, '#', sizeof(guarded));

  TEST_ASSERT(xbee_dump_format_value(xbee_at_table_find(XBEE_AT_SH),
                                     sh, sizeof(sh), guarded, cap)
              == guarded);

  // Terminated inside the capacity, and nothing beyond it disturbed.
  TEST_ASSERT(strlen(guarded) < (size_t)cap);
  for (i = cap; i < (uint16_t)sizeof(guarded); i++) {
    if (guarded[i] != '#') {
      TEST_FAIL("byte %u past the capacity was overwritten", (unsigned)i);
      break;
    }
  }
}

/***************************************************************************//**
 * The longest value in the command set still fits the rendering buffer.
 ******************************************************************************/
static void test_format_longest_value(void)
{
  uint8_t longest[XBEE_AT_VALUE_MAX];
  char out[XBEE_DUMP_VALUE_TEXT_CAP];
  const char *text;

  (void)memset(longest, 0xA5, sizeof(longest));

  // FK is the 65-byte public key, the widest parameter the table describes.
  text = render(XBEE_AT_FK, longest, (uint16_t)sizeof(longest), out);
  TEST_ASSERT_EQ_UINT(strlen(text), (uint64_t)sizeof(longest) * 2U);

  // The same length as text, where the decoration is widest.
  text = render(XBEE_AT_NI, longest, (uint16_t)sizeof(longest), out);
  TEST_ASSERT(strlen(text) < XBEE_DUMP_VALUE_TEXT_CAP);
}

/***************************************************************************//**
 * Entry point.
 ******************************************************************************/
int main(void)
{
  TEST_RUN(test_category_names);
  TEST_RUN(test_skip_rule);
  TEST_RUN(test_format_integers);
  TEST_RUN(test_format_width_comes_from_the_table);
  TEST_RUN(test_format_u64);
  TEST_RUN(test_format_bitmap);
  TEST_RUN(test_format_strings_and_bytes);
  TEST_RUN(test_format_edge_cases);
  TEST_RUN(test_format_respects_capacity);
  TEST_RUN(test_format_longest_value);

  return TEST_SUMMARY();
}
