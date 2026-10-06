/***************************************************************************//**
 * @file
 * @brief Application data kept in MCU NVM3, shared with the production firmware.
 ******************************************************************************/

#define APP_LOG_TAG  "nvm_store"

#include <stdint.h>
#include <string.h>

#include "nvm3_default.h"

#include "app_log.h"
#include "device_sn.h"
#include "nvm_store.h"

/***************************************************************************//**
 * Read the serial number object, accepting only what WTX-FW accepts.
 *
 * @param[out] out        DEVICE_SN_STR_LEN writable bytes.
 * @param[out] stored_len Size of the stored object when there is one. May be
 *                        NULL.
 ******************************************************************************/
static sl_status_t read_device_sn_object(char *out, size_t *stored_len)
{
  uint32_t type = 0U;
  size_t len = 0U;
  sl_status_t status;

  // Size before content, as WTX-FW does: an object of any other length is
  // "not set", never read partially or padded.
  status = nvm3_getObjectInfo(nvm3_defaultHandle, NVM_STORE_KEY_DEVICE_SN,
                              &type, &len);
  if (status != SL_STATUS_OK) {
    return status;
  }

  if (stored_len != NULL) {
    *stored_len = len;
  }

  if (type != NVM3_OBJECTTYPE_DATA) {
    return SL_STATUS_INVALID_TYPE;
  }
  if (len != DEVICE_SN_STR_LEN) {
    return SL_STATUS_INVALID_COUNT;
  }

  return nvm3_readData(nvm3_defaultHandle, NVM_STORE_KEY_DEVICE_SN,
                       out, DEVICE_SN_STR_LEN);
}

/***************************************************************************//**
 * Make sure the NVM3 default instance is open.
 ******************************************************************************/
sl_status_t nvm_store_init(void)
{
  sl_status_t status = nvm3_initDefault();

  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("NVM3 default instance is not open, status 0x%04X",
                  (unsigned)status);
  }

  return status;
}

/***************************************************************************//**
 * Read the device serial number object.
 ******************************************************************************/
sl_status_t nvm_store_read_device_sn(char *out, size_t cap, size_t *stored_len)
{
  sl_status_t status;

  if (out == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (cap < DEVICE_SN_STR_LEN) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  status = nvm_store_init();
  if (status != SL_STATUS_OK) {
    return status;
  }

  return read_device_sn_object(out, stored_len);
}

/***************************************************************************//**
 * Write the device serial number object, then verify it.
 ******************************************************************************/
sl_status_t nvm_store_write_device_sn(const char *sn)
{
  char readback[DEVICE_SN_STR_LEN];
  sl_status_t status;

  if (sn == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  // WTX-FW checks neither the form nor the check digit, so this is the last
  // place a bad serial number can be stopped.
  if (device_sn_validate(sn) != SL_STATUS_OK) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  status = nvm_store_init();
  if (status != SL_STATUS_OK) {
    return status;
  }

  // Synchronous by design. The write programs a few words, and at worst erases
  // one 8 kB page when NVM3 repacks; the CPU stalls on flash meanwhile. This is
  // a one-time factory step, run only with nothing else in flight, so it is not
  // worth a state machine. The XBee UART keeps receiving through UARTDRV's LDMA.
  status = nvm3_writeData(nvm3_defaultHandle, NVM_STORE_KEY_DEVICE_SN,
                          sn, DEVICE_SN_STR_LEN);
  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("writing the serial number failed, status 0x%04X",
                  (unsigned)status);
    return status;
  }

  status = read_device_sn_object(readback, NULL);
  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("reading the serial number back failed, status 0x%04X",
                  (unsigned)status);
    return status;
  }

  if (memcmp(readback, sn, DEVICE_SN_STR_LEN) != 0) {
    APP_LOG_ERROR("the serial number read back differs from what was written");
    return SL_STATUS_FAIL;
  }

  return SL_STATUS_OK;
}
