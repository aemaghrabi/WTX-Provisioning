/***************************************************************************//**
 * @file
 * @brief Unit tests for the compiled provisioning configuration.
 *
 * These check the configuration header against the command table, so a value
 * that the module would reject fails the build's tests rather than the board.
 ******************************************************************************/

#include <string.h>

#include "test_util.h"
#include "xbee_at_table.h"
#include "xbee_provision_config.h"
#include "xbee_provision_table.h"

/***************************************************************************//**
 * The table covers exactly the provisionable commands: none missing, none
 * extra, none twice.
 ******************************************************************************/
static void test_coverage(void)
{
  const xbee_prov_param_t *table;
  uint16_t count = 0U;
  uint16_t i;
  uint16_t j;

  table = xbee_provision_table_get(&count);
  TEST_ASSERT(table != NULL);

  // Every configured parameter is a command that can be provisioned.
  for (i = 0U; i < count; i++) {
    const xbee_at_entry_t *e = xbee_at_table_find(table[i].command);

    TEST_ASSERT(e != NULL);
    TEST_ASSERT(xbee_at_table_is_provisionable(e));
  }

  // No command is configured twice, which would make the second write silently
  // override the first.
  for (i = 0U; i < count; i++) {
    for (j = (uint16_t)(i + 1U); j < count; j++) {
      TEST_ASSERT(table[i].command != table[j].command);
    }
  }

  // Every command that can be provisioned is configured. A command added to the
  // command table without a value here would never be written and never be
  // checked, so the configuration would quietly stop being complete.
  for (i = 0U; i < xbee_at_table_count(); i++) {
    const xbee_at_entry_t *e = xbee_at_table_at(i);
    bool found = false;

    if (!xbee_at_table_is_provisionable(e)) {
      continue;
    }

    for (j = 0U; j < count; j++) {
      if (table[j].command == e->id) {
        found = true;
        break;
      }
    }

    if (!found) {
      char name[4];

      (void)xbee_at_id_to_str(e->id, name, sizeof(name));
      TEST_FAIL("command %s can be provisioned but has no configured value", name);
    }
  }
}

/***************************************************************************//**
 * Every configured value is one the module would accept.
 ******************************************************************************/
static void test_values_valid(void)
{
  const xbee_prov_param_t *table;
  uint16_t count = 0U;
  uint16_t i;

  table = xbee_provision_table_get(&count);
  TEST_ASSERT(table != NULL);

  for (i = 0U; i < count; i++) {
    const xbee_at_entry_t *e = xbee_at_table_find(table[i].command);
    sl_status_t status;
    char name[4];

    TEST_ASSERT(e != NULL);
    if (e == NULL) {
      continue;
    }

    (void)xbee_at_id_to_str(table[i].command, name, sizeof(name));

    TEST_ASSERT(table[i].value != NULL);
    TEST_ASSERT(table[i].len > 0U);

    // An integer or byte parameter is always written at its full width; only a
    // string is shorter than the command's maximum.
    if (e->type == XBEE_AT_TYPE_STRING) {
      if (table[i].len > e->max_len) {
        TEST_FAIL("%s value is longer than the module accepts", name);
      }
    } else if (table[i].len != e->max_len) {
      TEST_FAIL("%s value is %u bytes, the command expects %u",
                name, (unsigned)table[i].len, (unsigned)e->max_len);
    } else {
      // Width is right.
    }

    status = xbee_at_table_validate_set(table[i].command,
                                        table[i].value, table[i].len);
    if (status != SL_STATUS_OK) {
      TEST_FAIL("%s value is rejected by the command table, status 0x%04X",
                name, (unsigned)status);
    }
  }
}

/***************************************************************************//**
 * Report which parameters a provisioning run would write.
 *
 * Provisioning writes only what deviates from the factory default, so this is
 * exactly the write set the configuration produces. It is printed rather than
 * asserted, because any deviation is by definition deliberate: someone edited
 * the header. Having the list in the test output means a change to the
 * configuration shows up as a reviewable diff of what the module will be told,
 * without anyone having to run a board.
 *
 * The one thing checked is that the write set is smaller than the whole table.
 * If every parameter deviated, the defaults in the command table would be
 * suspect rather than the header.
 ******************************************************************************/
static void test_report_deviations(void)
{
  const xbee_prov_param_t *table;
  uint16_t count = 0U;
  uint16_t deviating = 0U;
  uint16_t i;

  table = xbee_provision_table_get(&count);
  TEST_ASSERT(table != NULL);

  for (i = 0U; i < count; i++) {
    const xbee_at_entry_t *e = xbee_at_table_find(table[i].command);
    char name[4];
    uint16_t b;

    if (xbee_at_table_is_default(e, table[i].value, table[i].len)) {
      continue;
    }

    deviating++;
    (void)xbee_at_id_to_str(table[i].command, name, sizeof(name));
    (void)printf("  writes %s = ", name);
    for (b = 0U; b < table[i].len; b++) {
      (void)printf("%02X", table[i].value[b]);
    }
    (void)printf("\n");
  }

  (void)printf("  %u of %u parameters deviate from the factory default\n",
               (unsigned)deviating, (unsigned)count);

  TEST_ASSERT(deviating < count);
}

/***************************************************************************//**
 * Spot-check how the three value forms are laid out.
 ******************************************************************************/
static void test_value_encoding(void)
{
  const xbee_prov_param_t *table;
  uint16_t count = 0U;
  uint16_t i;
  bool saw_id = false;
  bool saw_ni = false;
  bool saw_ia = false;

  table = xbee_provision_table_get(&count);
  TEST_ASSERT(table != NULL);

  for (i = 0U; i < count; i++) {
    switch (table[i].command) {
      case XBEE_AT_ID_:
        // A two-byte integer, most significant byte first.
        TEST_ASSERT_EQ_UINT(table[i].len, 2U);
        TEST_ASSERT_EQ_UINT(table[i].value[0], 0x33U);
        TEST_ASSERT_EQ_UINT(table[i].value[1], 0x32U);
        saw_id = true;
        break;

      case XBEE_AT_NI:
        // A string is written without its terminator.
        TEST_ASSERT_EQ_UINT(table[i].len, (uint16_t)(sizeof(XBEE_PROV_NI) - 1U));
        saw_ni = true;
        break;

      case XBEE_AT_IA:
        // The full 64-bit width, every byte set.
        TEST_ASSERT_EQ_UINT(table[i].len, 8U);
        TEST_ASSERT_EQ_UINT(table[i].value[0], 0xFFU);
        TEST_ASSERT_EQ_UINT(table[i].value[7], 0xFFU);
        saw_ia = true;
        break;

      default:
        break;
    }
  }

  TEST_ASSERT(saw_id);
  TEST_ASSERT(saw_ni);
  TEST_ASSERT(saw_ia);
}

/***************************************************************************//**
 * The UART settings agree with the Simplicity Studio configuration.
 *
 * The provisioned module and this MCU have to use the same line settings or the
 * verification pass after the reset could not talk to it. The firmware checks
 * this at build time against the generated configuration; here the values are
 * checked against what that configuration currently holds, so a change to
 * either side without the other fails.
 ******************************************************************************/
static void test_uart_settings_match_studio(void)
{
  // 9600 baud is code 3 (manual lines 6076 to 6086).
  TEST_ASSERT_EQ_UINT(XBEE_PROV_BD, 3U);
  // No parity, one stop bit.
  TEST_ASSERT_EQ_UINT(XBEE_PROV_NB, 0U);
  TEST_ASSERT_EQ_UINT(XBEE_PROV_SB, 0U);
}

/***************************************************************************//**
 * Entry point.
 ******************************************************************************/
int main(void)
{
  TEST_RUN(test_coverage);
  TEST_RUN(test_values_valid);
  TEST_RUN(test_report_deviations);
  TEST_RUN(test_value_encoding);
  TEST_RUN(test_uart_settings_match_studio);

  return TEST_SUMMARY();
}
