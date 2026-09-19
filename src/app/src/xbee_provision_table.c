/***************************************************************************//**
 * @file
 * @brief The configuration header compiled into wire-form parameter values.
 *
 * The parameter list in xbee_provision_list.h is expanded twice: once to define
 * a constant array holding each parameter's value as it travels to the module,
 * and once to build the table that points at those arrays. Naming a parameter
 * in the list is therefore enough; it cannot end up in one expansion and not
 * the other.
 ******************************************************************************/

#include <stddef.h>

#include "xbee_at_table.h"
#include "xbee_provision_config.h"
#include "xbee_provision_table.h"

#include "xbee_provision_list.h"

/// @name Big-endian initialisers
///
/// An integer parameter travels most significant byte first (manual lines 7176
/// to 7180), at the width its command expects. These build that byte sequence
/// at compile time, so the table holds no integers to convert at run time.
/// @{
#define XBEE_PROV_BE1(v)  { (uint8_t)(v) }
#define XBEE_PROV_BE2(v)  { (uint8_t)((uint64_t)(v) >> 8),  (uint8_t)(v) }
#define XBEE_PROV_BE4(v)  { (uint8_t)((uint64_t)(v) >> 24), (uint8_t)((uint64_t)(v) >> 16), \
                            (uint8_t)((uint64_t)(v) >> 8),  (uint8_t)(v) }
#define XBEE_PROV_BE8(v)  { (uint8_t)((uint64_t)(v) >> 56), (uint8_t)((uint64_t)(v) >> 48), \
                            (uint8_t)((uint64_t)(v) >> 40), (uint8_t)((uint64_t)(v) >> 32), \
                            (uint8_t)((uint64_t)(v) >> 24), (uint8_t)((uint64_t)(v) >> 16), \
                            (uint8_t)((uint64_t)(v) >> 8),  (uint8_t)(v) }
/// @}

/// Select the initialiser for a width. The indirection lets width_ arrive as a
/// macro argument rather than a literal.
#define XBEE_PROV_BE_(width_, v)  XBEE_PROV_BE##width_(v)
#define XBEE_PROV_BE(width_, v)   XBEE_PROV_BE_(width_, v)

// ---------------------------------------------------------------------------
// First expansion: one constant array per parameter.

/// An integer, laid out big-endian at the command's width.
#define DEFINE_INT(name_, id_, width_)                    \
  static const uint8_t prov_value_##name_[width_] =       \
    XBEE_PROV_BE(width_, XBEE_PROV_##name_);

/// A printable string, stored with its terminator so sizeof gives the length,
/// and written without it.
#define DEFINE_STR(name_, id_, width_)                    \
  static const char prov_value_##name_[] = XBEE_PROV_##name_;  \
  _Static_assert((sizeof(prov_value_##name_) - 1U) <= (width_),     \
                 "XBEE_PROV_" #name_ " is longer than the module accepts");

/// Opaque bytes, always at the command's full width. An initialiser that names
/// fewer bytes leaves the rest zero.
#define DEFINE_BYTES(name_, id_, width_)                  \
  static const uint8_t prov_value_##name_[width_] = XBEE_PROV_##name_;

XBEE_PROV_LIST(DEFINE_INT, DEFINE_STR, DEFINE_BYTES)

// ---------------------------------------------------------------------------
// Second expansion: the table.

#define ROW_INT(name_, id_, width_)                       \
  { (id_), prov_value_##name_, (uint8_t)(width_) },

#define ROW_STR(name_, id_, width_)                       \
  { (id_), (const uint8_t *)prov_value_##name_,           \
    (uint8_t)(sizeof(prov_value_##name_) - 1U) },

#define ROW_BYTES(name_, id_, width_)                     \
  { (id_), prov_value_##name_, (uint8_t)(width_) },

/// The configured value of every provisionable parameter.
static const xbee_prov_param_t prov_table[] = {
  XBEE_PROV_LIST(ROW_INT, ROW_STR, ROW_BYTES)
};

/// Number of entries in the table.
#define PROV_TABLE_COUNT  ((uint16_t)(sizeof(prov_table) / sizeof(prov_table[0])))

/***************************************************************************//**
 * The configured value of every provisionable parameter.
 ******************************************************************************/
const xbee_prov_param_t *xbee_provision_table_get(uint16_t *count)
{
  if (count == NULL) {
    return NULL;
  }

  *count = PROV_TABLE_COUNT;
  return prov_table;
}
