/***************************************************************************//**
 * @file
 * @brief Manufacturing data in the USERDATA page, read by the production firmware.
 ******************************************************************************/

#include <stdint.h>
#include <string.h>

#include "device_sn.h"
#include "userdata_flash.h"

#include "mfg_store.h"

/// Value of an erased flash byte.
#define MFG_STORE_ERASED_BYTE  0xFFU

/// Bytes programmed for a serial number: the stored bytes rounded up to whole
/// flash words. The bytes past the serial number are written as 0xFF, which
/// leaves them erased.
#define MFG_STORE_SN_WRITE_LEN                                    \
  (((DEVICE_SN_LEN + USERDATA_FLASH_WORD_SIZE) - 1U)              \
   / USERDATA_FLASH_WORD_SIZE * USERDATA_FLASH_WORD_SIZE)

_Static_assert(DEVICE_SN_LEN <= MFG_STORE_SN_SLOT_SIZE,
               "the serial number must fit its USERDATA slot");
_Static_assert(MFG_STORE_SN_WRITE_LEN <= MFG_STORE_SN_SLOT_SIZE,
               "the serial number write must stay inside its USERDATA slot");
_Static_assert(MFG_STORE_SN_WRITE_LEN <= USERDATA_FLASH_WRITE_MAX,
               "the serial number write must fit one USERDATA write");
_Static_assert((MFG_STORE_SN_OFFSET % USERDATA_FLASH_WORD_SIZE) == 0U,
               "the serial number slot must start on a flash word");

/***************************************************************************//**
 * Report whether every one of len bytes reads as erased flash.
 ******************************************************************************/
static bool all_erased(const uint8_t *bytes, size_t len)
{
  size_t i;

  for (i = 0U; i < len; i++) {
    if (bytes[i] != MFG_STORE_ERASED_BYTE) {
      return false;
    }
  }

  return true;
}

/***************************************************************************//**
 * Read the device serial number.
 ******************************************************************************/
sl_status_t mfg_store_read_device_sn(char *out, size_t cap)
{
  uint8_t stored[DEVICE_SN_LEN];
  sl_status_t status;

  if (out == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (cap < DEVICE_SN_STR_SIZE) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  status = userdata_flash_read(MFG_STORE_SN_OFFSET, stored, sizeof(stored));
  if (status != SL_STATUS_OK) {
    return status;
  }

  if (all_erased(stored, sizeof(stored))) {
    return SL_STATUS_NOT_FOUND;
  }

  (void)memcpy(out, stored, sizeof(stored));
  out[DEVICE_SN_LEN] = '\0';

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Report whether the whole serial number slot is still erased.
 ******************************************************************************/
sl_status_t mfg_store_device_sn_slot_is_blank(bool *blank)
{
  uint8_t slot[MFG_STORE_SN_SLOT_SIZE];
  sl_status_t status;

  if (blank == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  status = userdata_flash_read(MFG_STORE_SN_OFFSET, slot, sizeof(slot));
  if (status != SL_STATUS_OK) {
    return status;
  }

  *blank = all_erased(slot, sizeof(slot));

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Write the device serial number, then verify it.
 ******************************************************************************/
sl_status_t mfg_store_write_device_sn(const char *sn)
{
  uint8_t image[MFG_STORE_SN_WRITE_LEN];
  uint8_t readback[DEVICE_SN_LEN];
  bool blank = false;
  sl_status_t status;

  if (sn == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  // WTX-FW checks neither the form nor the check digit, so this is the last
  // place a bad serial number can be stopped.
  if (device_sn_validate(sn) != SL_STATUS_OK) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Programming only clears bits, so writing over anything but erased flash
  // would store a mix of the two values.
  status = mfg_store_device_sn_slot_is_blank(&blank);
  if (status != SL_STATUS_OK) {
    return status;
  }
  if (!blank) {
    return SL_STATUS_ALREADY_EXISTS;
  }

  (void)memset(image, MFG_STORE_ERASED_BYTE, sizeof(image));
  (void)memcpy(image, sn, DEVICE_SN_LEN);

  status = userdata_flash_write(MFG_STORE_SN_OFFSET, image, sizeof(image));
  if (status != SL_STATUS_OK) {
    return status;
  }

  status = userdata_flash_read(MFG_STORE_SN_OFFSET, readback, sizeof(readback));
  if (status != SL_STATUS_OK) {
    return status;
  }

  if (memcmp(readback, sn, DEVICE_SN_LEN) != 0) {
    return SL_STATUS_FAIL;
  }

  return SL_STATUS_OK;
}
