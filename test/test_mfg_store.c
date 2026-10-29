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

  TEST_ASSERT_EQ_MEM(&page[MFG_STORE_SN_OFFSET], sn_a, DEVICE_SN_LEN);
  // The rest of the slot, and the rest of the page, stay erased.
  TEST_ASSERT(page_erased(MFG_STORE_SN_OFFSET + DEVICE_SN_LEN,
                          MFG_STORE_SN_SLOT_SIZE - DEVICE_SN_LEN));
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
  (void)memcpy(&page[MFG_STORE_SN_OFFSET], "18072600123X", DEVICE_SN_LEN);

  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_OK);
  TEST_ASSERT_EQ_MEM(out, "18072600123X", DEVICE_SN_STR_SIZE);
  TEST_ASSERT_EQ_UINT(device_sn_validate(out), SL_STATUS_INVALID_PARAMETER);

  // Phase 1 rule: set as soon as any of the twelve bytes is not erased.
  fake_userdata_flash_reset();
  page = fake_userdata_flash_page();
  page[MFG_STORE_SN_OFFSET + DEVICE_SN_LEN - 1U] = '0';
  TEST_ASSERT_EQ_UINT(mfg_store_read_device_sn(out, sizeof(out)), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT((uint8_t)out[0], 0xFFU);
  TEST_ASSERT_EQ_UINT((uint8_t)out[DEVICE_SN_LEN], 0U);
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

int main(void)
{
  TEST_RUN(test_slot_layout);
  TEST_RUN(test_erased_slot);
  TEST_RUN(test_write_and_read);
  TEST_RUN(test_no_overwrite);
  TEST_RUN(test_neighbours_ignored);
  TEST_RUN(test_read_malformed);
  TEST_RUN(test_rejects);
  TEST_RUN(test_write_failure);
  return TEST_SUMMARY();
}
