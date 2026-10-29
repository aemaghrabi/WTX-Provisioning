/***************************************************************************//**
 * @file
 * @brief Application data kept in MCU NVM3, shared with the production firmware.
 *
 * Uses the NVM3 default instance (components nvm3_default and
 * nvm3_default_flash_backend), which sl_main opens at start-up with
 * nvm3_initDefault(). The production firmware (WTX-FW) uses the same instance
 * with the same configuration (config/nvm3_default_config.h: 40960 bytes, cache
 * 200, max object 254, headroom 0), so both images find it at the same
 * address, 0x080F4000, and see the same objects. Changing that configuration
 * in either project without the other breaks the hand-over.
 *
 * Keys 0x00001 (settings) and 0x00002 (radio state) belong to WTX-FW
 * (lcd_menu.c) and are never touched here.
 *
 * @note No longer used for the device serial number, which moved to the
 *       USERDATA page (src/services/inc/mfg_store.h, see
 *       docs/plan/2026-10-29-sn-userdata-storage.md). Nothing calls this module
 *       any more. Kept for now; to be deleted later.
 ******************************************************************************/

#ifndef NVM_STORE_H
#define NVM_STORE_H

#include <stddef.h>
#include "sl_status.h"

/// NVM3 key of the device serial number. Must equal NVM3_KEY_DEVICE_SN in
/// WTX-FW lcd_menu.c.
#define NVM_STORE_KEY_DEVICE_SN  0x00003UL

/***************************************************************************//**
 * Make sure the NVM3 default instance is open.
 *
 * sl_main has already tried at start-up but ignores the result. Opening again
 * with the same configuration returns SL_STATUS_OK without touching flash, and
 * retries the open if the start-up attempt failed. The read and write functions
 * below call this themselves.
 *
 * @return SL_STATUS_OK when the instance is open, otherwise the NVM3 status.
 ******************************************************************************/
sl_status_t nvm_store_init(void);

/***************************************************************************//**
 * Read the device serial number object.
 *
 * Only a data object of exactly DEVICE_SN_LEN bytes is accepted; WTX-FW must
 * apply the same rule when it reads the same object. The object has no NUL
 * terminator: its DEVICE_SN_LEN bytes are returned as stored, followed by a
 * NUL added in out. The content is not guaranteed to be well formed; check it
 * with device_sn_validate() before relying on it.
 *
 * @param[out] out        Destination, DEVICE_SN_STR_SIZE bytes on success.
 * @param[in]  cap        Capacity of out, at least DEVICE_SN_STR_SIZE.
 * @param[out] stored_len Size of the stored object, set whenever one exists,
 *                        including on SL_STATUS_INVALID_COUNT. May be NULL.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if out is NULL,
 *         SL_STATUS_WOULD_OVERFLOW if cap is below DEVICE_SN_STR_SIZE,
 *         SL_STATUS_NOT_FOUND if no object is stored under the key,
 *         SL_STATUS_INVALID_TYPE if a counter object is stored under it,
 *         SL_STATUS_INVALID_COUNT if the object is not DEVICE_SN_LEN bytes,
 *         otherwise the NVM3 status.
 ******************************************************************************/
sl_status_t nvm_store_read_device_sn(char *out, size_t cap, size_t *stored_len);

/***************************************************************************//**
 * Write the device serial number object, then read it back to verify it.
 *
 * Replaces whatever is stored under the key. Only a serial number that passes
 * device_sn_validate() is written, so a malformed value or a wrong check digit
 * never reaches the production firmware, which checks neither.
 *
 * @note Blocks while flash is programmed: a few word writes, and at worst one
 *       8 kB page erase if NVM3 has to repack. Meant for a one-time factory
 *       step, called with nothing else in flight.
 *
 * @param[in] sn DEVICE_SN_LEN bytes, "FFYYWWSSSSSC". Only those bytes are
 *               stored; no terminator is written.
 *
 * @return SL_STATUS_OK when the object was written and read back identical,
 *         SL_STATUS_NULL_POINTER if sn is NULL,
 *         SL_STATUS_INVALID_PARAMETER if sn is not a valid serial number,
 *         SL_STATUS_FAIL if the read-back differs,
 *         otherwise the NVM3 status of the failing step.
 ******************************************************************************/
sl_status_t nvm_store_write_device_sn(const char *sn);

#endif  // NVM_STORE_H
