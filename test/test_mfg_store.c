/***************************************************************************//**
 * @file
 * @brief Unit tests for the mfg_store service, over a simulated USERDATA page.
 *
 * The serial number vectors are ones device_sn and tools/provision.py agree
 * on: the check digit of 18072600123 is 0, and of 18264100123 is 1.
 ******************************************************************************/

#include <stdbool.h>

#include "test_util.h"
#include "device_sn.h"
#include "mfg_store.h"
#include "sensor_variant.h"
#include "userdata_flash.h"
#include "fake_userdata_flash.h"

/// A valid serial number.
static const char sn_a[] = "180726001230";

/// Another valid serial number.
static const char sn_b[] = "182641001231";

/***************************************************************************//**
 * Report whether len bytes of the page from offset are all erased.
 ******************************************************************************/
static bool page_erased(uint32_t offset, size_t len)
{
  const uint8_t *page = fake_userdata_flash_page();
  size_t i;

  for (i = 0U; i < len; i++) {
    if (page[offset + i] != 0xFFU) {
      return false;
    }
  }
  return true;
}

/***************************************************************************//**
 * The slot is the SDK's TOKEN_MFG_SERIAL_NUMBER, inside the page.
 ******************************************************************************/
static void test_slot_layout(void)
{
  TEST_ASSERT_EQ_UINT(MFG_STORE_SN_OFFSET, 0x08CU);
  TEST_ASSERT_EQ_UINT(MFG_STORE_SN_SLOT_SIZE, 16U);
  TEST_ASSERT((MFG_STORE_SN_OFFSET + MFG_STORE_SN_SLOT_SIZE) <= USERDATA_FLASH_SIZE);
}

/***************************************************************************//**
 * An erased slot reads as not set, and as blank.
 ******************************************************************************/
static void test_erased_slot(void)
{
  char out[DEVICE_SN_STR_SIZE];
  bool blank = false;

  fake_userdata_flash_reset();

  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_NOT_FOUND);
  TEST_ASSERT_EQ_UINT(mfg_store_device_sn_slot_is_blank(&blank), SL_STATUS_OK);
  TEST_ASSERT(blank);
}

/***************************************************************************//**
 * A write lands at the slot, reads back, and touches nothing else.
 ******************************************************************************/
static void test_write_and_read(void)
{
  const uint8_t *page;
  char out[DEVICE_SN_STR_SIZE];
  bool blank = true;

  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();

  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_a), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_userdata_flash_write_count(), 1U);

  // Exactly the 13 bytes WTX-FW reads: the digits, then the NUL.
  TEST_ASSERT_EQ_UINT(DEVICE_SN_STORED_LEN, 13U);
  TEST_ASSERT_EQ_MEM(&page[MFG_STORE_SN_OFFSET], sn_a, DEVICE_SN_STORED_LEN);
  TEST_ASSERT_EQ_UINT(page[MFG_STORE_SN_OFFSET + DEVICE_SN_LEN], 0x00U);
  // The rest of the slot, and the rest of the page, stay erased.
  TEST_ASSERT(page_erased(MFG_STORE_SN_OFFSET + DEVICE_SN_STORED_LEN,
                          MFG_STORE_SN_SLOT_SIZE - DEVICE_SN_STORED_LEN));
  TEST_ASSERT(page_erased(0U, MFG_STORE_SN_OFFSET));
  TEST_ASSERT(page_erased(MFG_STORE_SN_OFFSET + MFG_STORE_SN_SLOT_SIZE,
                          USERDATA_FLASH_SIZE - MFG_STORE_SN_OFFSET
                          - MFG_STORE_SN_SLOT_SIZE));

  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_OK);
  TEST_ASSERT_EQ_MEM(out, sn_a, DEVICE_SN_STR_SIZE);

  TEST_ASSERT_EQ_UINT(mfg_store_device_sn_slot_is_blank(&blank), SL_STATUS_OK);
  TEST_ASSERT(!blank);
}

/***************************************************************************//**
 * A slot that is not erased is never written, so two numbers never mix.
 ******************************************************************************/
static void test_no_overwrite(void)
{
  const uint8_t *page;

  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();

  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_a), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_b), SL_STATUS_ALREADY_EXISTS);
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_a), SL_STATUS_ALREADY_EXISTS);
  TEST_ASSERT_EQ_UINT(fake_userdata_flash_write_count(), 1U);
  TEST_ASSERT_EQ_MEM(&page[MFG_STORE_SN_OFFSET], sn_a, DEVICE_SN_LEN);

  // Anything at all in the slot counts, even past the serial number.
  fake_userdata_flash_reset();
  fake_userdata_flash_page()[MFG_STORE_SN_OFFSET + MFG_STORE_SN_SLOT_SIZE - 1U] = 0x00U;
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_a), SL_STATUS_ALREADY_EXISTS);
  TEST_ASSERT_EQ_UINT(fake_userdata_flash_write_count(), 0U);
  TEST_ASSERT(page_erased(MFG_STORE_SN_OFFSET, DEVICE_SN_LEN));
}

/***************************************************************************//**
 * Bytes outside the slot do not matter to it.
 ******************************************************************************/
static void test_neighbours_ignored(void)
{
  uint8_t *page;
  char out[DEVICE_SN_STR_SIZE];
  bool blank = false;

  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();
  page[MFG_STORE_SN_OFFSET - 1U] = 0x00U;
  page[MFG_STORE_SN_OFFSET + MFG_STORE_SN_SLOT_SIZE] = 0x00U;

  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_NOT_FOUND);
  TEST_ASSERT_EQ_UINT(mfg_store_device_sn_slot_is_blank(&blank), SL_STATUS_OK);
  TEST_ASSERT(blank);
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_a), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(page[MFG_STORE_SN_OFFSET - 1U], 0x00U);
  TEST_ASSERT_EQ_UINT(page[MFG_STORE_SN_OFFSET + MFG_STORE_SN_SLOT_SIZE], 0x00U);
}

/***************************************************************************//**
 * Whatever is stored is returned as it is, terminated, for the caller to judge.
 ******************************************************************************/
static void test_read_malformed(void)
{
  uint8_t *page;
  char out[DEVICE_SN_STR_SIZE];

  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();
  (void)memcpy(&page[MFG_STORE_SN_OFFSET], "18072600123X", DEVICE_SN_STORED_LEN);

  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_OK);
  TEST_ASSERT_EQ_MEM(out, "18072600123X", DEVICE_SN_STR_SIZE);
  TEST_ASSERT_EQ_UINT(device_sn_validate(out), SL_STATUS_INVALID_PARAMETER);

  // Only the first byte written, the rest erased: set, as WTX-FW sees it, and
  // malformed. The terminator is still erased, so that is what is reported.
  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();
  page[MFG_STORE_SN_OFFSET] = (uint8_t)'1';
  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_INVALID_COUNT);
  TEST_ASSERT_EQ_UINT((uint8_t)out[0], (uint8_t)'1');
  TEST_ASSERT_EQ_UINT((uint8_t)out[1], 0xFFU);
  TEST_ASSERT_EQ_UINT((uint8_t)out[DEVICE_SN_LEN], 0U);
  TEST_ASSERT_EQ_UINT(device_sn_validate(out), SL_STATUS_INVALID_PARAMETER);

  // First byte 0x00, which is not erased: set too, however empty it looks.
  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();
  page[MFG_STORE_SN_OFFSET] = 0x00U;
  page[MFG_STORE_SN_OFFSET + DEVICE_SN_LEN] = 0x00U;
  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(device_sn_validate(out), SL_STATUS_INVALID_PARAMETER);
}

/***************************************************************************//**
 * Not set is decided on the first byte alone, as WTX-FW decides it, and is not
 * the same as writable.
 ******************************************************************************/
static void test_not_set_rule(void)
{
  uint8_t *page;
  char out[DEVICE_SN_STR_SIZE];
  bool blank = true;

  // First byte erased, garbage after it: not set, and not blank.
  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();
  (void)memcpy(&page[MFG_STORE_SN_OFFSET + 1U], "8072600123", 10U);
  page[MFG_STORE_SN_OFFSET + DEVICE_SN_LEN] = 0x00U;

  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_NOT_FOUND);
  TEST_ASSERT_EQ_UINT(mfg_store_device_sn_slot_is_blank(&blank), SL_STATUS_OK);
  TEST_ASSERT(!blank);
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_a), SL_STATUS_ALREADY_EXISTS);
  TEST_ASSERT_EQ_UINT(fake_userdata_flash_write_count(), 0U);

  // First byte erased, only the last byte of the slot written: the same.
  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();
  page[MFG_STORE_SN_OFFSET + MFG_STORE_SN_SLOT_SIZE - 1U] = 0x00U;
  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_NOT_FOUND);
  TEST_ASSERT_EQ_UINT(mfg_store_device_sn_slot_is_blank(&blank), SL_STATUS_OK);
  TEST_ASSERT(!blank);
}

/***************************************************************************//**
 * Twelve good digits without the NUL after them are reported, not accepted:
 * WTX-FW would read past the digits.
 ******************************************************************************/
static void test_read_missing_terminator(void)
{
  uint8_t *page;
  char out[DEVICE_SN_STR_SIZE];

  // The terminator byte left erased, as a 12-byte write would leave it.
  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();
  (void)memcpy(&page[MFG_STORE_SN_OFFSET], sn_a, DEVICE_SN_LEN);

  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_INVALID_COUNT);
  // The digits still come back, terminated in RAM, for the caller to show.
  TEST_ASSERT_EQ_MEM(out, sn_a, DEVICE_SN_STR_SIZE);
  TEST_ASSERT_EQ_UINT(device_sn_validate(out), SL_STATUS_OK);

  // A byte other than 0x00 or 0xFF there is no better.
  page[MFG_STORE_SN_OFFSET + DEVICE_SN_LEN] = (uint8_t)'7';
  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_INVALID_COUNT);
  TEST_ASSERT_EQ_UINT((uint8_t)out[DEVICE_SN_LEN], 0U);

  // And such a slot is not writable.
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_a), SL_STATUS_ALREADY_EXISTS);
}

/***************************************************************************//**
 * Invalid serial numbers and bad arguments never reach the flash.
 ******************************************************************************/
static void test_rejects(void)
{
  char out[DEVICE_SN_STR_SIZE];

  fake_userdata_flash_reset();

  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(NULL), SL_STATUS_NULL_POINTER);
  // Wrong check digit.
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn("180726001234"), SL_STATUS_INVALID_PARAMETER);
  // The old YYWW-NNNNN-C form.
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn("2641-00123-5"), SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(fake_userdata_flash_write_count(), 0U);
  TEST_ASSERT(page_erased(0U, USERDATA_FLASH_SIZE));

  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(NULL, sizeof(out)), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, DEVICE_SN_LEN), SL_STATUS_WOULD_OVERFLOW);
  TEST_ASSERT_EQ_UINT(mfg_store_device_sn_slot_is_blank(NULL), SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * A flash failure is passed back unchanged.
 ******************************************************************************/
static void test_write_failure(void)
{
  fake_userdata_flash_reset();
  fake_userdata_flash_fail_writes(SL_STATUS_FLASH_PROGRAM_FAILED);

  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_a), SL_STATUS_FLASH_PROGRAM_FAILED);
  TEST_ASSERT(page_erased(0U, USERDATA_FLASH_SIZE));
}

/***************************************************************************//**
 * The page reads as blank only when every byte of it is erased, slot or not.
 ******************************************************************************/
static void test_page_blank(void)
{
  bool blank = false;

  fake_userdata_flash_reset();
  TEST_ASSERT_EQ_UINT(mfg_store_is_blank(&blank), SL_STATUS_OK);
  TEST_ASSERT(blank);

  // Outside the serial number slot counts too.
  fake_userdata_flash_page()[USERDATA_FLASH_SIZE - 1U] = 0x7FU;
  TEST_ASSERT_EQ_UINT(mfg_store_is_blank(&blank), SL_STATUS_OK);
  TEST_ASSERT(!blank);

  fake_userdata_flash_reset();
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_a), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(mfg_store_is_blank(&blank), SL_STATUS_OK);
  TEST_ASSERT(!blank);

  TEST_ASSERT_EQ_UINT(mfg_store_is_blank(NULL), SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * Erasing clears the whole page, after which a different serial number can be
 * written, which is the only way to replace one.
 ******************************************************************************/
static void test_erase_then_rewrite(void)
{
  uint8_t *page;
  char out[DEVICE_SN_STR_SIZE];
  bool blank = false;

  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_a), SL_STATUS_OK);
  // Something else in the page, which the erase takes with it.
  page[0] = 0x12U;
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_b), SL_STATUS_ALREADY_EXISTS);

  TEST_ASSERT_EQ_UINT(mfg_store_erase(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_userdata_flash_erase_count(), 1U);
  TEST_ASSERT(page_erased(0U, USERDATA_FLASH_SIZE));
  TEST_ASSERT_EQ_UINT(mfg_store_is_blank(&blank), SL_STATUS_OK);
  TEST_ASSERT(blank);
  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_NOT_FOUND);

  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_b), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_OK);
  TEST_ASSERT_EQ_MEM(out, sn_b, DEVICE_SN_STR_SIZE);
  TEST_ASSERT_EQ_MEM(&page[MFG_STORE_SN_OFFSET], sn_b, DEVICE_SN_STORED_LEN);
}

/***************************************************************************//**
 * A failed erase is passed back, and the page is left as it was.
 ******************************************************************************/
static void test_erase_failure(void)
{
  uint8_t *page;

  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_a), SL_STATUS_OK);
  fake_userdata_flash_fail_erases(SL_STATUS_FLASH_PROGRAM_FAILED);

  TEST_ASSERT_EQ_UINT(mfg_store_erase(), SL_STATUS_FLASH_PROGRAM_FAILED);
  TEST_ASSERT_EQ_UINT(fake_userdata_flash_erase_count(), 0U);
  TEST_ASSERT_EQ_MEM(&page[MFG_STORE_SN_OFFSET], sn_a, DEVICE_SN_STORED_LEN);
}

/***************************************************************************//**
 * The variant sits at the contract's address, clear of the serial number slot.
 ******************************************************************************/
static void test_variant_layout(void)
{
  TEST_ASSERT_EQ_UINT(MFG_STORE_VARIANT_OFFSET, 0x110U);
  TEST_ASSERT_EQ_UINT(MFG_STORE_VARIANT_SIZE, 2U);
  TEST_ASSERT((MFG_STORE_VARIANT_OFFSET % 4U) == 0U);
  TEST_ASSERT(MFG_STORE_VARIANT_OFFSET >= (MFG_STORE_SN_OFFSET + MFG_STORE_SN_SLOT_SIZE));
}

/***************************************************************************//**
 * An erased page reads both variant bytes as 0xFF, and as blank.
 ******************************************************************************/
static void test_variant_erased(void)
{
  sensor_variant_t v = { 0U, 0U };
  bool blank = false;

  fake_userdata_flash_reset();

  TEST_ASSERT_EQ_UINT(mfg_store_read_sensor_variant(&v), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(v.rtd, 0xFFU);
  TEST_ASSERT_EQ_UINT(v.pressure, 0xFFU);
  TEST_ASSERT_EQ_UINT(mfg_store_sensor_variant_is_blank(&blank), SL_STATUS_OK);
  TEST_ASSERT(blank);
}

/***************************************************************************//**
 * A written variant reads back, and nothing else in the page changes.
 ******************************************************************************/
static void test_variant_write_and_read(void)
{
  const sensor_variant_t want = { SENSOR_VARIANT_RTD_PRESENT,
                                  SENSOR_VARIANT_PRESSURE_1_5VDC };
  sensor_variant_t got = { 0U, 0U };
  bool blank = true;
  uint8_t *page;

  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();

  TEST_ASSERT_EQ_UINT(mfg_store_write_sensor_variant(&want), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(fake_userdata_flash_write_count(), 1U);
  TEST_ASSERT_EQ_UINT(page[0x110], 0x01U);
  TEST_ASSERT_EQ_UINT(page[0x111], 0x01U);
  TEST_ASSERT(page_erased(0x112U, 2U));
  // The buffer the contract keeps free, and everything before and after.
  TEST_ASSERT(page_erased(0U, 0x110U));
  TEST_ASSERT(page_erased(0x114U, USERDATA_FLASH_SIZE - 0x114U));

  TEST_ASSERT_EQ_UINT(mfg_store_read_sensor_variant(&got), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(got.rtd, 0x01U);
  TEST_ASSERT_EQ_UINT(got.pressure, 0x01U);
  TEST_ASSERT_EQ_UINT(mfg_store_sensor_variant_is_blank(&blank), SL_STATUS_OK);
  TEST_ASSERT(!blank);
}

/***************************************************************************//**
 * Every writable combination is accepted, written as given.
 ******************************************************************************/
static void test_variant_combinations(void)
{
  const uint8_t rtds[] = { SENSOR_VARIANT_RTD_ABSENT, SENSOR_VARIANT_RTD_PRESENT };
  const uint8_t pressures[] = { SENSOR_VARIANT_PRESSURE_NONE,
                                SENSOR_VARIANT_PRESSURE_1_5VDC };
  size_t r;
  size_t p;

  for (r = 0U; r < sizeof(rtds); r++) {
    for (p = 0U; p < sizeof(pressures); p++) {
      sensor_variant_t want;
      sensor_variant_t got = { 0U, 0U };

      want.rtd = rtds[r];
      want.pressure = pressures[p];
      fake_userdata_flash_reset();

      TEST_ASSERT_EQ_UINT(mfg_store_write_sensor_variant(&want), SL_STATUS_OK);
      TEST_ASSERT_EQ_UINT(mfg_store_read_sensor_variant(&got), SL_STATUS_OK);
      TEST_ASSERT_EQ_UINT(got.rtd, rtds[r]);
      TEST_ASSERT_EQ_UINT(got.pressure, pressures[p]);
    }
  }
}

/***************************************************************************//**
 * A variant is written only over erased bytes, even when only one is written.
 ******************************************************************************/
static void test_variant_no_overwrite(void)
{
  const sensor_variant_t a = { SENSOR_VARIANT_RTD_PRESENT, SENSOR_VARIANT_PRESSURE_NONE };
  const sensor_variant_t b = { SENSOR_VARIANT_RTD_ABSENT, SENSOR_VARIANT_PRESSURE_1_5VDC };
  uint8_t *page;

  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();

  TEST_ASSERT_EQ_UINT(mfg_store_write_sensor_variant(&a), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(mfg_store_write_sensor_variant(&b), SL_STATUS_ALREADY_EXISTS);
  TEST_ASSERT_EQ_UINT(mfg_store_write_sensor_variant(&a), SL_STATUS_ALREADY_EXISTS);
  TEST_ASSERT_EQ_UINT(fake_userdata_flash_write_count(), 1U);
  TEST_ASSERT_EQ_UINT(page[0x110], 0x01U);
  TEST_ASSERT_EQ_UINT(page[0x111], 0x00U);

  // One byte alone, set by hand, is enough to refuse.
  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();
  page[0x111] = 0x01U;
  TEST_ASSERT_EQ_UINT(mfg_store_write_sensor_variant(&a), SL_STATUS_ALREADY_EXISTS);
  TEST_ASSERT_EQ_UINT(fake_userdata_flash_write_count(), 0U);
}

/***************************************************************************//**
 * Reserved, erased and stray values are refused before anything is written.
 ******************************************************************************/
static void test_variant_rejects(void)
{
  const sensor_variant_t bad[] = {
    { SENSOR_VARIANT_RTD_PRESENT, SENSOR_VARIANT_PRESSURE_MODBUS },
    { SENSOR_VARIANT_RTD_PRESENT, SENSOR_VARIANT_PRESSURE_MV_BRIDGE },
    { SENSOR_VARIANT_RTD_PRESENT, SENSOR_VARIANT_UNSET },
    { SENSOR_VARIANT_UNSET, SENSOR_VARIANT_PRESSURE_NONE },
    { 0x02U, SENSOR_VARIANT_PRESSURE_NONE },
    { SENSOR_VARIANT_RTD_ABSENT, 0x04U },
  };
  size_t i;

  fake_userdata_flash_reset();

  for (i = 0U; i < (sizeof(bad) / sizeof(bad[0])); i++) {
    TEST_ASSERT_EQ_UINT(mfg_store_write_sensor_variant(&bad[i]),
                        SL_STATUS_INVALID_PARAMETER);
  }
  TEST_ASSERT_EQ_UINT(mfg_store_write_sensor_variant(NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(mfg_store_read_sensor_variant(NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(mfg_store_sensor_variant_is_blank(NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(fake_userdata_flash_write_count(), 0U);
  TEST_ASSERT(page_erased(0U, USERDATA_FLASH_SIZE));
}

/***************************************************************************//**
 * A failing flash write is passed back, and the bytes stay erased.
 ******************************************************************************/
static void test_variant_write_failure(void)
{
  const sensor_variant_t v = { SENSOR_VARIANT_RTD_PRESENT, SENSOR_VARIANT_PRESSURE_1_5VDC };
  bool blank = false;

  fake_userdata_flash_reset();
  fake_userdata_flash_fail_writes(SL_STATUS_FLASH_PROGRAM_FAILED);

  TEST_ASSERT_EQ_UINT(mfg_store_write_sensor_variant(&v), SL_STATUS_FLASH_PROGRAM_FAILED);
  TEST_ASSERT_EQ_UINT(mfg_store_sensor_variant_is_blank(&blank), SL_STATUS_OK);
  TEST_ASSERT(blank);
}

/***************************************************************************//**
 * The serial number and the variant are independent, and both survive in the
 * page together. An erase takes both, and both can then be written again.
 ******************************************************************************/
static void test_variant_with_serial_number(void)
{
  const sensor_variant_t v = { SENSOR_VARIANT_RTD_PRESENT, SENSOR_VARIANT_PRESSURE_1_5VDC };
  sensor_variant_t got = { 0U, 0U };
  char out[DEVICE_SN_STR_SIZE];

  fake_userdata_flash_reset();

  // The order of a unit provisioned before the variant existed: the serial
  // number is already there when the variant is added.
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_a), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(mfg_store_write_sensor_variant(&v), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_OK);
  TEST_ASSERT_EQ_MEM(out, sn_a, DEVICE_SN_STR_SIZE);
  TEST_ASSERT_EQ_UINT(mfg_store_read_sensor_variant(&got), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(got.rtd, 0x01U);
  TEST_ASSERT_EQ_UINT(got.pressure, 0x01U);

  TEST_ASSERT_EQ_UINT(mfg_store_erase(), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_NOT_FOUND);
  TEST_ASSERT_EQ_UINT(mfg_store_read_sensor_variant(&got), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(got.rtd, 0xFFU);
  TEST_ASSERT_EQ_UINT(got.pressure, 0xFFU);

  // The hex build's order: variant, then serial number.
  TEST_ASSERT_EQ_UINT(mfg_store_write_sensor_variant(&v), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(mfg_store_write_device_sn(sn_b), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_OK);
  TEST_ASSERT_EQ_MEM(out, sn_b, DEVICE_SN_STR_SIZE);
  TEST_ASSERT_EQ_UINT(mfg_store_read_sensor_variant(&got), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(got.rtd, 0x01U);
}

int main(void)
{
  TEST_RUN(test_slot_layout);
  TEST_RUN(test_erased_slot);
  TEST_RUN(test_write_and_read);
  TEST_RUN(test_no_overwrite);
  TEST_RUN(test_neighbours_ignored);
  TEST_RUN(test_read_malformed);
  TEST_RUN(test_read_missing_terminator);
  TEST_RUN(test_not_set_rule);
  TEST_RUN(test_rejects);
  TEST_RUN(test_write_failure);
  TEST_RUN(test_page_blank);
  TEST_RUN(test_erase_then_rewrite);
  TEST_RUN(test_erase_failure);
  TEST_RUN(test_variant_layout);
  TEST_RUN(test_variant_erased);
  TEST_RUN(test_variant_write_and_read);
  TEST_RUN(test_variant_combinations);
  TEST_RUN(test_variant_no_overwrite);
  TEST_RUN(test_variant_rejects);
  TEST_RUN(test_variant_write_failure);
  TEST_RUN(test_variant_with_serial_number);
  return TEST_SUMMARY();
}
