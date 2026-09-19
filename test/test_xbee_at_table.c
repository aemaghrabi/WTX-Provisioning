/***************************************************************************//**
 * @file
 * @brief Unit tests for the xbee_at_table command table.
 ******************************************************************************/

#include <string.h>

#include "test_util.h"
#include "xbee_at_table.h"

/// Commands the manual documents, counted across its 24 categories.
#define EXPECTED_COMMAND_COUNT  147U

/// Commands that hold a writable, stored value with a documented default.
///
/// This is the set the provisioning configuration header must cover, so it is
/// pinned here as well as in test_xbee_provision_table.c.
#define EXPECTED_PROVISIONABLE_COUNT  108U

/***************************************************************************//**
 * The table holds every documented command, and no identifier repeats.
 ******************************************************************************/
static void test_table_integrity(void)
{
  uint16_t count = xbee_at_table_count();
  uint16_t i;
  uint16_t j;

  TEST_ASSERT_EQ_UINT(count, EXPECTED_COMMAND_COUNT);

  for (i = 0U; i < count; i++) {
    const xbee_at_entry_t *e = xbee_at_table_at(i);

    TEST_ASSERT(e != NULL);
    if (e == NULL) {
      continue;
    }

    // Both characters are printable ASCII, as the manual's command names are.
    TEST_ASSERT((uint8_t)(e->id >> 8) >= 0x21U);
    TEST_ASSERT((uint8_t)(e->id >> 8) <= 0x7EU);
    TEST_ASSERT((uint8_t)e->id >= 0x21U);
    TEST_ASSERT((uint8_t)e->id <= 0x7EU);

    TEST_ASSERT(e->category < XBEE_AT_CAT_COUNT);
    TEST_ASSERT(e->max_len <= XBEE_AT_VALUE_MAX);
    TEST_ASSERT(e->min <= e->max);

    // An executable command has no value, everything else has one.
    if (e->type == XBEE_AT_TYPE_EXEC) {
      TEST_ASSERT_EQ_UINT(e->max_len, 0U);
    } else {
      TEST_ASSERT(e->max_len > 0U);
    }

    // A command cannot be both read only and write only.
    TEST_ASSERT((e->flags & (XBEE_AT_FLAG_READ_ONLY | XBEE_AT_FLAG_WRITE_ONLY))
                != (XBEE_AT_FLAG_READ_ONLY | XBEE_AT_FLAG_WRITE_ONLY));

    // A stated default must fall inside the stated range.
    if (((e->flags & XBEE_AT_FLAG_NO_DEFAULT) == 0U)
        && (e->type != XBEE_AT_TYPE_EXEC)
        && (e->type != XBEE_AT_TYPE_STRING)
        && (e->type != XBEE_AT_TYPE_BYTES)
        && (e->type != XBEE_AT_TYPE_SUBCOMMAND)
        && (e->type != XBEE_AT_TYPE_U64)) {
      TEST_ASSERT(e->default_value >= e->min);
      TEST_ASSERT(e->default_value <= e->max);
    }

    // Identifiers are unique, so a lookup is unambiguous.
    for (j = (uint16_t)(i + 1U); j < count; j++) {
      const xbee_at_entry_t *other = xbee_at_table_at(j);

      TEST_ASSERT(other != NULL);
      if (other != NULL) {
        TEST_ASSERT(e->id != other->id);
      }
    }
  }

  TEST_ASSERT(xbee_at_table_at(count) == NULL);
  TEST_ASSERT(xbee_at_table_at(0xFFFFU) == NULL);
}

/***************************************************************************//**
 * Every category the manual defines is represented.
 ******************************************************************************/
static void test_all_categories_present(void)
{
  bool seen[XBEE_AT_CAT_COUNT] = { false };
  uint16_t i;

  for (i = 0U; i < xbee_at_table_count(); i++) {
    const xbee_at_entry_t *e = xbee_at_table_at(i);

    if (e != NULL) {
      seen[e->category] = true;
    }
  }

  for (i = 0U; i < (uint16_t)XBEE_AT_CAT_COUNT; i++) {
    TEST_ASSERT(seen[i]);
  }
}

/***************************************************************************//**
 * Spot-check entries against the manual.
 ******************************************************************************/
static void test_known_entries(void)
{
  const xbee_at_entry_t *e;

  // CH: channels 0x0B to 0x1A, default 0x0C (manual lines 4843 to 4852).
  e = xbee_at_table_find(XBEE_AT_CH);
  TEST_ASSERT(e != NULL);
  TEST_ASSERT_EQ_UINT(e->min, 0x0BU);
  TEST_ASSERT_EQ_UINT(e->max, 0x1AU);
  TEST_ASSERT_EQ_UINT(e->default_value, 0x0CU);
  TEST_ASSERT_EQ_UINT(e->category, XBEE_AT_CAT_NETWORKING);

  // ID: default extended PAN identifier 0x3332 (lines 4853 to 4862).
  e = xbee_at_table_find(XBEE_AT_ID_);
  TEST_ASSERT(e != NULL);
  TEST_ASSERT_EQ_UINT(e->default_value, 0x3332U);
  TEST_ASSERT_EQ_UINT(e->max_len, 2U);

  // AO defaults to 2, the legacy output mode (lines 6014 to 6037).
  e = xbee_at_table_find(XBEE_AT_AO);
  TEST_ASSERT(e != NULL);
  TEST_ASSERT_EQ_UINT(e->default_value, 2U);
  TEST_ASSERT_EQ_UINT(e->max, 2U);

  // AP: 0 transparent through 4 MicroPython (lines 5996 to 6013).
  e = xbee_at_table_find(XBEE_AT_AP);
  TEST_ASSERT(e != NULL);
  TEST_ASSERT_EQ_UINT(e->default_value, 0U);
  TEST_ASSERT_EQ_UINT(e->max, 4U);

  // BD defaults to 3, which is 9600 baud (lines 6060 to 6089).
  e = xbee_at_table_find(XBEE_AT_BD);
  TEST_ASSERT(e != NULL);
  TEST_ASSERT_EQ_UINT(e->default_value, 3U);

  // Command mode parameters (lines 6139 to 6179).
  e = xbee_at_table_find(XBEE_AT_CC);
  TEST_ASSERT(e != NULL);
  TEST_ASSERT_EQ_UINT(e->default_value, 0x2BU);  // the plus character
  e = xbee_at_table_find(XBEE_AT_CT);
  TEST_ASSERT(e != NULL);
  TEST_ASSERT_EQ_UINT(e->default_value, 0x64U);  // 10 seconds
  TEST_ASSERT_EQ_UINT(e->min, 2U);
  e = xbee_at_table_find(XBEE_AT_GT);
  TEST_ASSERT(e != NULL);
  TEST_ASSERT_EQ_UINT(e->default_value, 0x3E8U);  // 1 second
  TEST_ASSERT_EQ_UINT(e->max, 0x6D3U);

  // NI is a printable string of at most 20 characters (lines 4953 to 4967).
  e = xbee_at_table_find(XBEE_AT_NI);
  TEST_ASSERT(e != NULL);
  TEST_ASSERT_EQ_UINT(e->type, XBEE_AT_TYPE_STRING);
  TEST_ASSERT_EQ_UINT(e->max_len, 20U);

  // FK is a 65-byte key that must be set locally (lines 5896 to 5916).
  e = xbee_at_table_find(XBEE_AT_FK);
  TEST_ASSERT(e != NULL);
  TEST_ASSERT_EQ_UINT(e->max_len, 65U);
  TEST_ASSERT((e->flags & XBEE_AT_FLAG_LOCAL_ONLY) != 0U);

  // IA disables I/O line passing with all 64 bits set (lines 6795 to 6803).
  // The default needs the full width, which a 32-bit field would truncate.
  e = xbee_at_table_find(XBEE_AT_IA);
  TEST_ASSERT(e != NULL);
  TEST_ASSERT_EQ_UINT(e->max_len, 8U);
  TEST_ASSERT(e->default_value == 0xFFFFFFFFFFFFFFFFULL);

  // A command that is not in the manual is not in the table.
  TEST_ASSERT(xbee_at_table_find(XBEE_AT_ID('Z', 'Z')) == NULL);
}

/***************************************************************************//**
 * Access flags match the manual's restrictions.
 ******************************************************************************/
static void test_access_flags(void)
{
  const xbee_at_entry_t *e;
  const uint16_t read_only[] = {
    XBEE_AT_SH, XBEE_AT_SL, XBEE_AT_NP, XBEE_AT_AI, XBEE_AT_DB, XBEE_AT_PP,
    XBEE_AT_VR, XBEE_AT_VL, XBEE_AT_VH, XBEE_AT_HV, XBEE_AT_R_QUERY,
    XBEE_AT_PCT_C, XBEE_AT_PCT_V, XBEE_AT_TP, XBEE_AT_CK, XBEE_AT_D_PCT,
    XBEE_AT_BL
  };
  const uint16_t local_only[] = {
    XBEE_AT_PCT_F, XBEE_AT_BANG_C, XBEE_AT_R1, XBEE_AT_FK, XBEE_AT_FS,
    XBEE_AT_PCT_P, XBEE_AT_ND, XBEE_AT_DN, XBEE_AT_AS, XBEE_AT_ED
  };
  const uint16_t executable[] = {
    XBEE_AT_AC, XBEE_AT_WR, XBEE_AT_FR, XBEE_AT_RE, XBEE_AT_CN, XBEE_AT_DA,
    XBEE_AT_FP, XBEE_AT_IS
  };
  uint16_t i;

  for (i = 0U; i < (uint16_t)(sizeof(read_only) / sizeof(read_only[0])); i++) {
    e = xbee_at_table_find(read_only[i]);
    TEST_ASSERT(e != NULL);
    if (e != NULL) {
      TEST_ASSERT((e->flags & XBEE_AT_FLAG_READ_ONLY) != 0U);
    }
  }

  for (i = 0U; i < (uint16_t)(sizeof(local_only) / sizeof(local_only[0])); i++) {
    e = xbee_at_table_find(local_only[i]);
    TEST_ASSERT(e != NULL);
    TEST_ASSERT(!xbee_at_table_is_remotable(local_only[i]));
  }

  for (i = 0U; i < (uint16_t)(sizeof(executable) / sizeof(executable[0])); i++) {
    e = xbee_at_table_find(executable[i]);
    TEST_ASSERT(e != NULL);
    if (e != NULL) {
      TEST_ASSERT_EQ_UINT(e->type, XBEE_AT_TYPE_EXEC);
    }
  }

  // KY is write only: reading it returns a status, never the key
  // (manual lines 5443 to 5453).
  e = xbee_at_table_find(XBEE_AT_KY);
  TEST_ASSERT(e != NULL);
  if (e != NULL) {
    TEST_ASSERT((e->flags & XBEE_AT_FLAG_WRITE_ONLY) != 0U);
  }

  // CA needs a write and a reset to take effect (lines 5578 to 5586).
  e = xbee_at_table_find(XBEE_AT_CA);
  TEST_ASSERT(e != NULL);
  if (e != NULL) {
    TEST_ASSERT((e->flags & XBEE_AT_FLAG_NEEDS_WR_RESET) != 0U);
  }

  // FS only works from Command mode (lines 5810 to 5814).
  e = xbee_at_table_find(XBEE_AT_FS);
  TEST_ASSERT(e != NULL);
  if (e != NULL) {
    TEST_ASSERT((e->flags & XBEE_AT_FLAG_CMD_MODE_ONLY) != 0U);
  }

  // Ordinary commands can be sent to a remote node.
  TEST_ASSERT(xbee_at_table_is_remotable(XBEE_AT_CH));
  TEST_ASSERT(xbee_at_table_is_remotable(XBEE_AT_NI));
  TEST_ASSERT(!xbee_at_table_is_remotable(XBEE_AT_ID('Z', 'Z')));
}

/***************************************************************************//**
 * Query validation rejects what cannot be read.
 ******************************************************************************/
static void test_validate_get(void)
{
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_get(XBEE_AT_SH), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_get(XBEE_AT_CH), SL_STATUS_OK);

  // Write only.
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_get(XBEE_AT_KY), SL_STATUS_PERMISSION);
  // Nothing to read back from an executable command.
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_get(XBEE_AT_WR), SL_STATUS_PERMISSION);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_get(XBEE_AT_AC), SL_STATUS_PERMISSION);

  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_get(XBEE_AT_ID('Z', 'Z')),
                      SL_STATUS_NOT_FOUND);
}

/***************************************************************************//**
 * Write validation enforces the documented ranges and lengths.
 ******************************************************************************/
static void test_validate_set(void)
{
  const uint8_t one[1] = { 0x0CU };
  const uint8_t too_low[1] = { 0x0AU };
  const uint8_t too_high[1] = { 0x1BU };
  const uint8_t two[2] = { 0x33U, 0x32U };
  const uint8_t long_value[24] = { 0 };

  // CH accepts channels 0x0B to 0x1A.
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_CH, one, 1U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_CH, too_low, 1U),
                      SL_STATUS_INVALID_RANGE);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_CH, too_high, 1U),
                      SL_STATUS_INVALID_RANGE);
  // A value wider than the command takes.
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_CH, two, 2U),
                      SL_STATUS_INVALID_PARAMETER);
  // A missing value.
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_CH, NULL, 0U),
                      SL_STATUS_INVALID_PARAMETER);

  // ID takes two bytes, and a one-byte value is accepted as a big-endian
  // integer with the leading zero omitted.
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_ID_, two, 2U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_ID_, one, 1U), SL_STATUS_OK);

  // Read-only commands reject any write.
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_SH, two, 2U),
                      SL_STATUS_PERMISSION);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_VR, two, 2U),
                      SL_STATUS_PERMISSION);

  // Executable commands take no parameter.
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_WR, NULL, 0U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_WR, one, 1U),
                      SL_STATUS_INVALID_PARAMETER);

  // NI accepts up to 20 characters, and an empty value clears it.
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_NI, long_value, 20U),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_NI, long_value, 21U),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_NI, NULL, 0U), SL_STATUS_OK);

  // KY is write only, so a write is allowed and bounded at 16 bytes.
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_KY, long_value, 16U),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_KY, long_value, 17U),
                      SL_STATUS_INVALID_PARAMETER);

  // Sleep mode 6 is listed in the value table although the printed range stops
  // at 5 (manual lines 5655 to 5676).
  {
    const uint8_t six[1] = { 6U };
    const uint8_t seven[1] = { 7U };

    TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_SM, six, 1U), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_SM, seven, 1U),
                        SL_STATUS_INVALID_RANGE);
  }

  // A 32-bit range is checked across the whole width.
  {
    const uint8_t sp_max[4] = { 0x00U, 0x15U, 0xF9U, 0x00U };
    const uint8_t sp_over[4] = { 0x00U, 0x15U, 0xF9U, 0x01U };

    TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_SP, sp_max, 4U),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_SP, sp_over, 4U),
                        SL_STATUS_INVALID_RANGE);
  }

  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_ID('Z', 'Z'), one, 1U),
                      SL_STATUS_NOT_FOUND);
  TEST_ASSERT_EQ_UINT(xbee_at_table_validate_set(XBEE_AT_CH, NULL, 1U),
                      SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * Identifier helpers round trip.
 ******************************************************************************/
static void test_id_helpers(void)
{
  char text[4];

  TEST_ASSERT_EQ_UINT(XBEE_AT_ID('S', 'H'), 0x5348U);
  TEST_ASSERT_EQ_UINT(XBEE_AT_ID('N', 'I'), 0x4E49U);
  // The punctuation commands pack the same way.
  TEST_ASSERT_EQ_UINT(XBEE_AT_DOLLAR_S, 0x2453U);
  TEST_ASSERT_EQ_UINT(XBEE_AT_STAR_S, 0x2A53U);
  TEST_ASSERT_EQ_UINT(XBEE_AT_R_QUERY, 0x523FU);
  TEST_ASSERT_EQ_UINT(XBEE_AT_BANG_C, 0x2143U);
  TEST_ASSERT_EQ_UINT(XBEE_AT_D_PCT, 0x4425U);

  TEST_ASSERT_EQ_UINT(xbee_at_id_to_str(XBEE_AT_SH, text, sizeof(text)),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_STR(text, "SH");
  TEST_ASSERT_EQ_UINT(xbee_at_id_to_str(XBEE_AT_BANG_C, text, sizeof(text)),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_STR(text, "!C");

  TEST_ASSERT_EQ_UINT(xbee_at_id_to_str(XBEE_AT_SH, text, 2U),
                      SL_STATUS_WOULD_OVERFLOW);
  TEST_ASSERT_EQ_UINT(xbee_at_id_to_str(XBEE_AT_SH, NULL, sizeof(text)),
                      SL_STATUS_NULL_POINTER);

  TEST_ASSERT(xbee_at_table_name(xbee_at_table_find(XBEE_AT_SH)) != NULL);
  TEST_ASSERT(xbee_at_table_name(NULL) != NULL);
}

/***************************************************************************//**
 * Commands that belong in a stored configuration are recognised as such.
 ******************************************************************************/
static void test_provisionable(void)
{
  uint16_t count = 0U;
  uint16_t i;

  // Commands that hold a writable, stored value with a documented default.
  const uint16_t provisionable[] = {
    XBEE_AT_CH, XBEE_AT_ID_, XBEE_AT_NI, XBEE_AT_MY, XBEE_AT_EE, XBEE_AT_KY,
    XBEE_AT_AP, XBEE_AT_AO, XBEE_AT_BD, XBEE_AT_CC, XBEE_AT_IA, XBEE_AT_FK,
    XBEE_AT_SM, XBEE_AT_D8, XBEE_AT_D9, XBEE_AT_LX,
  };
  // Read-only values, executable commands, text subcommands, counters that do
  // not survive a reset, and commands the manual gives no default for.
  const uint16_t not_provisionable[] = {
    XBEE_AT_SH, XBEE_AT_SL, XBEE_AT_VR, XBEE_AT_HV, XBEE_AT_NP,  // read only
    XBEE_AT_WR, XBEE_AT_AC, XBEE_AT_RE, XBEE_AT_FR, XBEE_AT_CN,  // executable
    XBEE_AT_FS, XBEE_AT_PY,                                      // subcommands
    XBEE_AT_EA, XBEE_AT_EC,                                      // volatile
    XBEE_AT_IO, XBEE_AT_CB,                                      // no default
  };

  for (i = 0U; i < (uint16_t)(sizeof(provisionable) / sizeof(provisionable[0])); i++) {
    TEST_ASSERT(xbee_at_table_is_provisionable(xbee_at_table_find(provisionable[i])));
  }
  for (i = 0U; i < (uint16_t)(sizeof(not_provisionable) / sizeof(not_provisionable[0])); i++) {
    TEST_ASSERT(!xbee_at_table_is_provisionable(xbee_at_table_find(not_provisionable[i])));
  }

  TEST_ASSERT(!xbee_at_table_is_provisionable(NULL));

  // The provisionable set is a fixed part of the configuration header, so its
  // size is pinned here: a table change that alters it must be deliberate.
  for (i = 0U; i < xbee_at_table_count(); i++) {
    if (xbee_at_table_is_provisionable(xbee_at_table_at(i))) {
      count++;
    }
  }
  TEST_ASSERT_EQ_UINT(count, EXPECTED_PROVISIONABLE_COUNT);
}

/***************************************************************************//**
 * Factory defaults are recognised across the value types.
 ******************************************************************************/
static void test_is_default(void)
{
  const xbee_at_entry_t *e;
  const uint8_t zeros[16] = { 0U };
  const uint8_t ones[16] = { 0xFFU };
  uint8_t buf[8];

  // An integer compared numerically: CH defaults to 0x0C.
  e = xbee_at_table_find(XBEE_AT_CH);
  buf[0] = 0x0CU;
  TEST_ASSERT(xbee_at_table_is_default(e, buf, 1U));
  buf[0] = 0x0DU;
  TEST_ASSERT(!xbee_at_table_is_default(e, buf, 1U));

  // A wider integer still matches when the leading zero byte is omitted, which
  // is how Command mode prints it. ID defaults to 0x3332.
  e = xbee_at_table_find(XBEE_AT_ID_);
  buf[0] = 0x33U;
  buf[1] = 0x32U;
  TEST_ASSERT(xbee_at_table_is_default(e, buf, 2U));

  // AO defaults to 2, and a one-byte read matches the stored default.
  e = xbee_at_table_find(XBEE_AT_AO);
  buf[0] = 2U;
  TEST_ASSERT(xbee_at_table_is_default(e, buf, 1U));

  // The 64-bit default that motivated widening the field.
  e = xbee_at_table_find(XBEE_AT_IA);
  (void)memset(buf, 0xFF, sizeof(buf));
  TEST_ASSERT(xbee_at_table_is_default(e, buf, 8U));
  buf[7] = 0xFEU;
  TEST_ASSERT(!xbee_at_table_is_default(e, buf, 8U));

  // A string parameter defaults to a single space.
  e = xbee_at_table_find(XBEE_AT_NI);
  TEST_ASSERT(xbee_at_table_is_default(e, (const uint8_t *)" ", 1U));
  TEST_ASSERT(!xbee_at_table_is_default(e, (const uint8_t *)"node", 4U));
  TEST_ASSERT(!xbee_at_table_is_default(e, (const uint8_t *)"", 0U));

  // A byte parameter defaults to all zeros: no key set.
  e = xbee_at_table_find(XBEE_AT_KY);
  TEST_ASSERT(xbee_at_table_is_default(e, zeros, 16U));
  TEST_ASSERT(xbee_at_table_is_default(e, NULL, 0U));
  TEST_ASSERT(!xbee_at_table_is_default(e, ones, 16U));

  // Without a documented default there is nothing to compare against.
  TEST_ASSERT(!xbee_at_table_is_default(xbee_at_table_find(XBEE_AT_IO), zeros, 1U));
  TEST_ASSERT(!xbee_at_table_is_default(NULL, zeros, 1U));
}

/***************************************************************************//**
 * Values compare numerically for integers and exactly for everything else.
 ******************************************************************************/
static void test_values_equal(void)
{
  const xbee_at_entry_t *e;
  const uint8_t padded[2] = { 0x00U, 0x2BU };
  const uint8_t bare[1] = { 0x2BU };
  const uint8_t other[1] = { 0x2CU };

  // The case this exists for: Command mode prints a parameter without leading
  // zeros, so a two-byte value read back in one byte is still the same value.
  e = xbee_at_table_find(XBEE_AT_CT);
  TEST_ASSERT(xbee_at_table_values_equal(e, padded, 2U, bare, 1U));
  TEST_ASSERT(xbee_at_table_values_equal(e, bare, 1U, padded, 2U));
  TEST_ASSERT(!xbee_at_table_values_equal(e, padded, 2U, other, 1U));

  // An omitted value reads as zero.
  e = xbee_at_table_find(XBEE_AT_MY);
  TEST_ASSERT(xbee_at_table_values_equal(e, NULL, 0U, (const uint8_t *)"\0", 1U));

  // Strings compare exactly, including length.
  e = xbee_at_table_find(XBEE_AT_NI);
  TEST_ASSERT(xbee_at_table_values_equal(e, (const uint8_t *)"node", 4U,
                                         (const uint8_t *)"node", 4U));
  TEST_ASSERT(!xbee_at_table_values_equal(e, (const uint8_t *)"node", 4U,
                                          (const uint8_t *)"nodes", 5U));
  TEST_ASSERT(!xbee_at_table_values_equal(e, (const uint8_t *)"node", 4U,
                                          (const uint8_t *)"Node", 4U));

  // Byte parameters compare exactly too.
  e = xbee_at_table_find(XBEE_AT_KY);
  TEST_ASSERT(xbee_at_table_values_equal(e, bare, 1U, bare, 1U));
  TEST_ASSERT(!xbee_at_table_values_equal(e, bare, 1U, padded, 2U));

  TEST_ASSERT(!xbee_at_table_values_equal(NULL, bare, 1U, bare, 1U));
}

/***************************************************************************//**
 * Entry point.
 ******************************************************************************/
int main(void)
{
  TEST_RUN(test_table_integrity);
  TEST_RUN(test_all_categories_present);
  TEST_RUN(test_known_entries);
  TEST_RUN(test_access_flags);
  TEST_RUN(test_validate_get);
  TEST_RUN(test_validate_set);
  TEST_RUN(test_id_helpers);
  TEST_RUN(test_provisionable);
  TEST_RUN(test_is_default);
  TEST_RUN(test_values_equal);

  return TEST_SUMMARY();
}
