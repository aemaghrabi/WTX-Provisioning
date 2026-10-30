/***************************************************************************//**
 * @file
 * @brief Sensor hardware variant: the two bytes WTX-FW reads from USERDATA.
 *
 * The production firmware (WTX-FW) is one binary for every assembly variant.
 * Two bytes in USERDATA (src/services/inc/mfg_store.h) tell it which sensors
 * this unit has:
 * - the RTD byte: whether a 3-wire PT100 is populated;
 * - the pressure sensor byte: which single pressure sensor type is populated.
 *
 * The axes are independent. The pressure byte is a type selector, not a
 * bitmask: at most one type is ever present.
 *
 * Values, as fixed by the contract. Existing values never change meaning; a new
 * pressure type gets a new value.
 *
 * | Byte     | Value | Meaning                                              |
 * | -------- | ----- | ---------------------------------------------------- |
 * | RTD      | 0x00  | not populated                                        |
 * | RTD      | 0x01  | populated                                            |
 * | Pressure | 0x00  | none                                                 |
 * | Pressure | 0x01  | 1-5VDC (Wika)                                        |
 * | Pressure | 0x02  | Modbus, reserved: WTX-FW does not support it yet     |
 * | Pressure | 0x03  | mV bridge, reserved: WTX-FW does not support it yet  |
 * | either   | 0xFF  | erased flash: WTX-FW treats it as not populated      |
 *
 * This module writes only the values marked writable in its tables. A reserved
 * type is recognised when reading, so it can be named, but is never accepted
 * for writing until a WTX-FW revision documents support for it.
 *
 * The module is hardware independent and depends only on sl_status.h and the C
 * standard library, so it also builds for the host unit tests in test/.
 *
 * Plan: docs/plan/2026-10-30-sensor-variant-userdata.md.
 ******************************************************************************/

#ifndef SENSOR_VARIANT_H
#define SENSOR_VARIANT_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

/// @name RTD byte values
/// @{
#define SENSOR_VARIANT_RTD_ABSENT         0x00U  ///< RTD not populated.
#define SENSOR_VARIANT_RTD_PRESENT        0x01U  ///< RTD populated.
/// @}

/// @name Pressure sensor byte values
/// @{
#define SENSOR_VARIANT_PRESSURE_NONE      0x00U  ///< No pressure sensor.
#define SENSOR_VARIANT_PRESSURE_1_5VDC    0x01U  ///< 1-5VDC (Wika).
#define SENSOR_VARIANT_PRESSURE_MODBUS    0x02U  ///< Modbus. Reserved, never written.
#define SENSOR_VARIANT_PRESSURE_MV_BRIDGE 0x03U  ///< mV bridge. Reserved, never written.
/// @}

/// Value of either byte in erased flash.
#define SENSOR_VARIANT_UNSET              0xFFU

/// The two variant bytes, as stored.
typedef struct {
  uint8_t rtd;       ///< One of SENSOR_VARIANT_RTD_*, or the raw stored byte.
  uint8_t pressure;  ///< One of SENSOR_VARIANT_PRESSURE_*, or the raw stored byte.
} sensor_variant_t;

/***************************************************************************//**
 * Turn the word typed for the RTD into its byte value.
 *
 * Accepted words, in any case: "present", "absent".
 *
 * @param[in]  word Typed word.
 * @param[out] rtd  Byte value, written on SL_STATUS_OK only.
 *
 * @return SL_STATUS_OK on a match,
 *         SL_STATUS_NULL_POINTER if word or rtd is NULL,
 *         SL_STATUS_INVALID_PARAMETER if the word is not accepted.
 ******************************************************************************/
sl_status_t sensor_variant_parse_rtd(const char *word, uint8_t *rtd);

/***************************************************************************//**
 * Turn the word typed for the pressure sensor into its byte value.
 *
 * Accepted words, in any case: "none", "1-5vdc". The names of the reserved
 * types ("modbus", "mv-bridge") are not accepted.
 *
 * @param[in]  word     Typed word.
 * @param[out] pressure Byte value, written on SL_STATUS_OK only.
 *
 * @return SL_STATUS_OK on a match,
 *         SL_STATUS_NULL_POINTER if word or pressure is NULL,
 *         SL_STATUS_INVALID_PARAMETER if the word is not accepted.
 ******************************************************************************/
sl_status_t sensor_variant_parse_pressure(const char *word, uint8_t *pressure);

/***************************************************************************//**
 * Check that a variant may be written: both bytes are writable values.
 *
 * @param[in] variant Variant to check.
 *
 * @return SL_STATUS_OK when both bytes are writable,
 *         SL_STATUS_NULL_POINTER if variant is NULL,
 *         SL_STATUS_INVALID_PARAMETER otherwise, including the reserved types
 *         and the erased value.
 ******************************************************************************/
sl_status_t sensor_variant_validate(const sensor_variant_t *variant);

/***************************************************************************//**
 * Describe an RTD byte, whatever it holds.
 *
 * @param[in] rtd Stored byte.
 *
 * @return A static string: "present", "absent", "not set" for the erased value,
 *         or "invalid" for anything else. Never NULL.
 ******************************************************************************/
const char *sensor_variant_rtd_name(uint8_t rtd);

/***************************************************************************//**
 * Describe a pressure sensor byte, whatever it holds.
 *
 * @param[in] pressure Stored byte.
 *
 * @return A static string: "none", "1-5VDC", "Modbus (reserved)",
 *         "mV bridge (reserved)", "not set" for the erased value, or "invalid"
 *         for anything else. Never NULL.
 ******************************************************************************/
const char *sensor_variant_pressure_name(uint8_t pressure);

#endif  // SENSOR_VARIANT_H
