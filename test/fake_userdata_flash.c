/***************************************************************************//**
 * @file
 * @brief Host stand-in for the USERDATA flash driver.
 *
 * Replaces src/drivers/src/userdata_flash.c, and checks its arguments the same
 * way, so the code under test sees the same errors it would on the board.
 ******************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "userdata_flash.h"
#include "fake_userdata_flash.h"

/// The page.
static uint8_t page[USERDATA_FLASH_SIZE];

/// Status every write returns while not SL_STATUS_OK.
static sl_status_t write_failure = SL_STATUS_OK;

/// Writes that reached the page.
static uint32_t writes;

/// Status every erase returns while not SL_STATUS_OK.
static sl_status_t erase_failure = SL_STATUS_OK;

/// Erases that reached the page.
static uint32_t erases;

/***************************************************************************//**
 * Report whether len bytes from offset lie inside the page.
 ******************************************************************************/
static bool in_page(uint32_t offset, size_t len)
{
  return (offset <= USERDATA_FLASH_SIZE)
         && (len <= (size_t)(USERDATA_FLASH_SIZE - offset));
}

void fake_userdata_flash_reset(void)
{
  (void)memset(page, 0xFF, sizeof(page));
  write_failure = SL_STATUS_OK;
  writes = 0U;
  erase_failure = SL_STATUS_OK;
  erases = 0U;
}

void fake_userdata_flash_fail_erases(sl_status_t status)
{
  erase_failure = status;
}

uint32_t fake_userdata_flash_erase_count(void)
{
  return erases;
}

uint8_t *fake_userdata_flash_page(void)
{
  return page;
}

void fake_userdata_flash_fail_writes(sl_status_t status)
{
  write_failure = status;
}

uint32_t fake_userdata_flash_write_count(void)
{
  return writes;
}

sl_status_t userdata_flash_read(uint32_t offset, void *out, size_t len)
{
  if (out == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (len == 0U) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (!in_page(offset, len)) {
    return SL_STATUS_INVALID_RANGE;
  }

  (void)memcpy(out, &page[offset], len);
  return SL_STATUS_OK;
}

sl_status_t userdata_flash_write(uint32_t offset, const void *data, size_t len)
{
  const uint8_t *src = data;
  size_t i;

  if (data == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if ((len == 0U)
      || (len > USERDATA_FLASH_WRITE_MAX)
      || ((len % USERDATA_FLASH_WORD_SIZE) != 0U)
      || ((offset % USERDATA_FLASH_WORD_SIZE) != 0U)) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (!in_page(offset, len)) {
    return SL_STATUS_INVALID_RANGE;
  }
  if (write_failure != SL_STATUS_OK) {
    return write_failure;
  }

  // Programming only clears bits.
  for (i = 0U; i < len; i++) {
    page[offset + i] &= src[i];
  }
  writes++;

  return SL_STATUS_OK;
}

sl_status_t userdata_flash_is_erased(bool *erased)
{
  size_t i;

  if (erased == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  for (i = 0U; i < sizeof(page); i++) {
    if (page[i] != 0xFFU) {
      *erased = false;
      return SL_STATUS_OK;
    }
  }

  *erased = true;
  return SL_STATUS_OK;
}

sl_status_t userdata_flash_erase(void)
{
  if (erase_failure != SL_STATUS_OK) {
    return erase_failure;
  }

  (void)memset(page, 0xFF, sizeof(page));
  erases++;

  return SL_STATUS_OK;
}
