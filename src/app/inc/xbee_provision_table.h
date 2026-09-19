/***************************************************************************//**
 * @file
 * @brief The configuration header compiled into wire-form parameter values.
 *
 * Turns the macros of xbee_provision_config.h into a constant table the
 * provisioning sequence can walk: one entry per parameter, holding the command
 * identifier and the value exactly as it travels to the module, integers
 * big-endian at the width the command expects.
 *
 * The table lives in flash and is built entirely at compile time, so reading it
 * costs nothing and it cannot disagree with the header.
 *
 * @note This module is hardware independent. It depends only on the command
 *       table, sl_status.h and the C standard library, so it also builds for
 *       the host unit tests, where the values are checked against the command
 *       table's documented ranges and widths.
 ******************************************************************************/

#ifndef XBEE_PROVISION_TABLE_H
#define XBEE_PROVISION_TABLE_H

#include <stdint.h>

/// One parameter of the configuration.
typedef struct {
  uint16_t command;       ///< Packed command characters, see the XBEE_AT_* names.
  const uint8_t *value;   ///< Value as it travels, big-endian for an integer.
  uint8_t len;            ///< Value length in bytes.
} xbee_prov_param_t;

/***************************************************************************//**
 * The configured value of every provisionable parameter.
 *
 * In the order of the manual's AT command chapter, which is also the order the
 * parameters are read and written, so a log of a provisioning run can be
 * followed against the manual.
 *
 * @param[out] count Number of entries. Must not be NULL.
 *
 * @return The table, or NULL if count is NULL. Never changes.
 ******************************************************************************/
const xbee_prov_param_t *xbee_provision_table_get(uint16_t *count);

#endif  // XBEE_PROVISION_TABLE_H
