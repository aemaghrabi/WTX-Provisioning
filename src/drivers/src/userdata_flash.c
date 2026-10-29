/***************************************************************************//**
 * @file
 * @brief Read and program the USERDATA flash page through the MSC.
 ******************************************************************************/

#define APP_LOG_TAG  "userdata"

#include <stdbool.h>
#include <string.h>

#include "em_device.h"
#include "em_msc.h"

#include "app_log.h"
#include "userdata_flash.h"

_Static_assert(USERDATA_FLASH_SIZE == USERDATA_SIZE,
               "USERDATA_FLASH_SIZE must match the device's USERDATA_SIZE");

_Static_assert((USERDATA_FLASH_WRITE_MAX % USERDATA_FLASH_WORD_SIZE) == 0U,
               "USERDATA_FLASH_WRITE_MAX must be whole words");

/// Words in the largest single write.
#define USERDATA_FLASH_WRITE_MAX_WORDS \
  (USERDATA_FLASH_WRITE_MAX / USERDATA_FLASH_WORD_SIZE)

/***************************************************************************//**
 * Report whether len bytes from offset lie inside the page.
 ******************************************************************************/
static bool in_page(uint32_t offset, size_t len)
{
  return (offset <= USERDATA_FLASH_SIZE)
         && (len <= (size_t)(USERDATA_FLASH_SIZE - offset));
}

/***************************************************************************//**
 * Turn an MSC result into an sl_status_t.
 ******************************************************************************/
static sl_status_t from_msc(MSC_Status_TypeDef msc)
{
  switch (msc) {
    case mscReturnOk:
      return SL_STATUS_OK;
    case mscReturnInvalidAddr:
      return SL_STATUS_INVALID_RANGE;
    case mscReturnLocked:
      APP_LOG_ERROR("USERDATA is locked: MSC_MISCLOCKWORD.UDLOCKBIT is set, "
                    "or the MSC registers are locked");
      return SL_STATUS_FLASH_PROGRAM_FAILED;
    case mscReturnTimeOut:
      return SL_STATUS_TIMEOUT;
    default:
      return SL_STATUS_FAIL;
  }
}

/***************************************************************************//**
 * Copy bytes out of the USERDATA page.
 ******************************************************************************/
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

  // The page is memory mapped; reading it needs no MSC operation.
  (void)memcpy(out, (const void *)(USERDATA_BASE + offset), len);

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Program bytes into the USERDATA page.
 ******************************************************************************/
sl_status_t userdata_flash_write(uint32_t offset, const void *data, size_t len)
{
  // MSC_WriteWord() reads its source a word at a time, so it gets an aligned
  // copy whatever the caller's buffer is.
  uint32_t words[USERDATA_FLASH_WRITE_MAX_WORDS];
  MSC_Status_TypeDef msc;

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

  (void)memcpy(words, data, len);

  // Init and Deinit around the one operation, as the SDK token manager does.
  // NVM3 shares the MSC, and its own calls unlock it and enable its clock
  // again, so leaving it locked afterwards does not get in its way.
  MSC_Init();
  msc = MSC_WriteWord((uint32_t *)(USERDATA_BASE + offset), words,
                      (uint32_t)len);
  MSC_Deinit();

  return from_msc(msc);
}
