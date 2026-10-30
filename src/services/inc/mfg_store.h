/***************************************************************************//**
 * @file
 * @brief Manufacturing data in the USERDATA page, read by the production firmware.
 *
 * The device serial number lives in the Simplicity SDK's reserved
 * manufacturing token slot TOKEN_MFG_SERIAL_NUMBER: offset 0x08C into
 * USERDATA, 16 bytes, so at 0x0FE0008C
 * (platform/service/token_manager/inc/sl_token_manager_manufacturing.h). The
 * production firmware (WTX-FW) reads it from there with a plain memory read.
 * USERDATA is not part of a main flash mass erase, so the serial number
 * survives reflashing.
 *
 * Stored form: exactly DEVICE_SN_STORED_LEN (13) bytes at the start of the
 * slot, the twelve digits "FFYYWWSSSSSC" followed by one NUL (0x00). WTX-FW
 * reads exactly those 13 bytes. The remaining bytes of the slot are left
 * erased.
 *
 * "Not set" means the first byte of the slot reads 0xFF, the erased state of
 * flash. That is the only check WTX-FW makes (raw flash carries no length or
 * existence metadata), so this module decides it the same way: whatever
 * follows a 0xFF first byte is ignored, and anything else counts as set, well
 * formed or not.
 *
 * Flash cannot be rewritten in place: a serial number is written only into a
 * slot whose 16 bytes are all still erased. "Not set" is therefore not the same
 * as writable: a slot whose first byte is erased but whose other bytes are not
 * reads as not set and still cannot be written. Replacing a serial number needs
 * the USERDATA page erased first, with mfg_store_erase().
 *
 * The sensor hardware variant (src/utils/inc/sensor_variant.h) is two bytes at
 * offset 0x110 (RTD) and 0x111 (pressure sensor type). That address is WTX-FW's
 * own, not an SDK token slot. The bytes 0x108 to 0x10F are a buffer the contract
 * keeps free for a future SDK token and are never written. A byte reading 0xFF
 * is "not populated". Both bytes are programmed in one flash word, once between
 * erases; as with the serial number, they are written only while both still read
 * erased.
 *
 * Plans: docs/plan/2026-10-29-sn-userdata-storage.md,
 *        docs/plan/2026-10-30-sensor-variant-userdata.md.
 ******************************************************************************/

#ifndef MFG_STORE_H
#define MFG_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include "sl_status.h"
#include "sensor_variant.h"

/// Offset of the serial number slot in USERDATA. Must equal
/// TOKEN_MFG_SERIAL_NUMBER & 0x0FFF in the SDK token manager.
#define MFG_STORE_SN_OFFSET     0x08CU

/// Size of the serial number slot reserved by the SDK, in bytes.
#define MFG_STORE_SN_SLOT_SIZE  16U

/// Offset of the RTD byte in USERDATA; the pressure sensor byte follows it.
#define MFG_STORE_VARIANT_OFFSET  0x110U

/// Bytes of the sensor variant: the RTD byte, then the pressure sensor byte.
#define MFG_STORE_VARIANT_SIZE    2U

/***************************************************************************//**
 * Read the device serial number.
 *
 * The DEVICE_SN_LEN digit bytes are returned as they are, followed by a NUL
 * in out, whatever the stored terminator byte holds. They are not guaranteed
 * to be well formed; check them with device_sn_validate() before relying on
 * them, and print them through device_sn_to_printable().
 *
 * @param[out] out Destination, DEVICE_SN_STR_SIZE bytes, written on
 *                 SL_STATUS_OK and on SL_STATUS_INVALID_COUNT.
 * @param[in]  cap Capacity of out, at least DEVICE_SN_STR_SIZE.
 *
 * @return SL_STATUS_OK when a serial number is stored with its NUL,
 *         SL_STATUS_INVALID_COUNT when something is stored but the byte after
 *         the digits is not the NUL; WTX-FW would read past the digits,
 *         SL_STATUS_NULL_POINTER if out is NULL,
 *         SL_STATUS_WOULD_OVERFLOW if cap is below DEVICE_SN_STR_SIZE,
 *         SL_STATUS_NOT_FOUND if none is stored: the first byte reads 0xFF,
 *         otherwise the status of the flash read.
 ******************************************************************************/
sl_status_t mfg_store_read_device_sn(char *out, size_t cap);

/***************************************************************************//**
 * Report whether the whole serial number slot is still erased, so that a
 * serial number can be written into it.
 *
 * @param[out] blank true when all MFG_STORE_SN_SLOT_SIZE bytes read 0xFF.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if blank is NULL,
 *         otherwise the status of the flash read.
 ******************************************************************************/
sl_status_t mfg_store_device_sn_slot_is_blank(bool *blank);

/***************************************************************************//**
 * Write the device serial number, then read it back to verify it.
 *
 * Only a serial number that passes device_sn_validate() is written, so a
 * malformed value or a wrong check digit never reaches the production
 * firmware, which checks neither. Only an erased slot is written.
 *
 * @note Blocks while flash is programmed, a few words. Meant for a one-time
 *       factory step, called with nothing else in flight.
 *
 * @param[in] sn DEVICE_SN_LEN digits, "FFYYWWSSSSSC". The NUL that follows
 *               them in flash is written here; sn need not be terminated.
 *
 * @return SL_STATUS_OK when all DEVICE_SN_STORED_LEN bytes were written and
 *         read back identical,
 *         SL_STATUS_NULL_POINTER if sn is NULL,
 *         SL_STATUS_INVALID_PARAMETER if sn is not a valid serial number,
 *         SL_STATUS_ALREADY_EXISTS if the slot is not erased; nothing is
 *         written,
 *         SL_STATUS_FAIL if the read-back differs,
 *         otherwise the status of the failing flash step.
 ******************************************************************************/
sl_status_t mfg_store_write_device_sn(const char *sn);

/***************************************************************************//**
 * Read the sensor variant bytes as stored.
 *
 * The bytes are returned raw, so an erased 0xFF or a value this firmware would
 * not write shows as it is. Name them with sensor_variant_rtd_name() and
 * sensor_variant_pressure_name().
 *
 * @param[out] variant Destination.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if variant is NULL,
 *         otherwise the status of the flash read.
 ******************************************************************************/
sl_status_t mfg_store_read_sensor_variant(sensor_variant_t *variant);

/***************************************************************************//**
 * Report whether both sensor variant bytes are still erased, so that a variant
 * can be written.
 *
 * @param[out] blank true when both bytes read 0xFF.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if blank is NULL,
 *         otherwise the status of the flash read.
 ******************************************************************************/
sl_status_t mfg_store_sensor_variant_is_blank(bool *blank);

/***************************************************************************//**
 * Write the sensor variant, then read it back to verify it.
 *
 * Only values sensor_variant_validate() accepts are written, so a reserved
 * pressure type or a stray value never reaches the production firmware. Only
 * erased bytes are written. One flash word is programmed: the two bytes and two
 * bytes of 0xFF, which leaves the neighbouring bytes erased.
 *
 * @note Blocks while flash is programmed, one word. Meant for a one-time
 *       factory step, called with nothing else in flight.
 *
 * @param[in] variant Bytes to store.
 *
 * @return SL_STATUS_OK when both bytes were written and read back identical,
 *         SL_STATUS_NULL_POINTER if variant is NULL,
 *         SL_STATUS_INVALID_PARAMETER if variant is not writable,
 *         SL_STATUS_ALREADY_EXISTS if either byte is not erased; nothing is
 *         written,
 *         SL_STATUS_FAIL if the read-back differs,
 *         otherwise the status of the failing flash step.
 ******************************************************************************/
sl_status_t mfg_store_write_sensor_variant(const sensor_variant_t *variant);

/***************************************************************************//**
 * Report whether the whole USERDATA page is erased, so that there is nothing
 * for mfg_store_erase() to do.
 *
 * @param[out] blank true when every byte of the page reads 0xFF.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if blank is NULL.
 ******************************************************************************/
sl_status_t mfg_store_is_blank(bool *blank);

/***************************************************************************//**
 * Erase the whole USERDATA page, serial number included, and check that it
 * reads as erased afterwards.
 *
 * Flash can only be erased a page at a time, so this removes everything
 * stored in USERDATA, not only the serial number: the same effect as
 * "commander device pageerase --region @userdata".
 *
 * @note Blocks for one page erase. Meant for a factory step, called with
 *       nothing else in flight.
 *
 * @return SL_STATUS_OK when the page was erased and reads as erased,
 *         otherwise the status of userdata_flash_erase().
 ******************************************************************************/
sl_status_t mfg_store_erase(void);

#endif  // MFG_STORE_H
