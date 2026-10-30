/***************************************************************************//**
 * @file
 * @brief Sensor hardware variant: the two bytes WTX-FW reads from USERDATA.
 ******************************************************************************/

#include <stddef.h>

#include "sensor_variant.h"

/// One value of one axis.
typedef struct {
  uint8_t     value;     ///< Byte stored in flash.
  const char *word;      ///< Lower-case word typed by the operator.
  const char *name;      ///< Description printed back.
  bool        writable;  ///< False for a type reserved for a later revision.
} variant_entry_t;

/// Name for the erased value.
static const char name_unset[] = "not set";

/// Name for a byte that is neither a table value nor erased.
static const char name_invalid[] = "invalid";

static const variant_entry_t rtd_table[] = {
  { SENSOR_VARIANT_RTD_ABSENT,  "absent",  "absent",  true },
  { SENSOR_VARIANT_RTD_PRESENT, "present", "present", true },
};

static const variant_entry_t pressure_table[] = {
  { SENSOR_VARIANT_PRESSURE_NONE,      "none",      "none",                 true },
  { SENSOR_VARIANT_PRESSURE_1_5VDC,    "1-5vdc",    "1-5VDC",               true },
  { SENSOR_VARIANT_PRESSURE_MODBUS,    "modbus",    "Modbus (reserved)",    false },
  { SENSOR_VARIANT_PRESSURE_MV_BRIDGE, "mv-bridge", "mV bridge (reserved)", false },
};

#define TABLE_COUNT(t)  ((uint8_t)(sizeof(t) / sizeof((t)[0])))

/***************************************************************************//**
 * Lower-case an ASCII letter and leave everything else alone.
 ******************************************************************************/
static char lower(char c)
{
  return ((c >= 'A') && (c <= 'Z')) ? (char)(c - 'A' + 'a') : c;
}

/***************************************************************************//**
 * Report whether typed equals the lower-case word, ignoring case.
 ******************************************************************************/
static bool word_matches(const char *typed, const char *word)
{
  while ((*typed != '\0') && (*word != '\0')) {
    if (lower(*typed) != *word) {
      return false;
    }
    typed++;
    word++;
  }

  return (*typed == '\0') && (*word == '\0');
}

/***************************************************************************//**
 * Find a writable entry by its typed word.
 ******************************************************************************/
static sl_status_t parse(const variant_entry_t *table, uint8_t count,
                         const char *word, uint8_t *out)
{
  uint8_t i;

  if ((word == NULL) || (out == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }

  for (i = 0U; i < count; i++) {
    if (table[i].writable && word_matches(word, table[i].word)) {
      *out = table[i].value;
      return SL_STATUS_OK;
    }
  }

  return SL_STATUS_INVALID_PARAMETER;
}

/***************************************************************************//**
 * Report whether value is a writable entry of the table.
 ******************************************************************************/
static bool is_writable(const variant_entry_t *table, uint8_t count,
                        uint8_t value)
{
  uint8_t i;

  for (i = 0U; i < count; i++) {
    if (table[i].writable && (table[i].value == value)) {
      return true;
    }
  }

  return false;
}

/***************************************************************************//**
 * Describe a stored byte of one axis.
 ******************************************************************************/
static const char *describe(const variant_entry_t *table, uint8_t count,
                            uint8_t value)
{
  uint8_t i;

  for (i = 0U; i < count; i++) {
    if (table[i].value == value) {
      return table[i].name;
    }
  }

  return (value == SENSOR_VARIANT_UNSET) ? name_unset : name_invalid;
}

sl_status_t sensor_variant_parse_rtd(const char *word, uint8_t *rtd)
{
  return parse(rtd_table, TABLE_COUNT(rtd_table), word, rtd);
}

sl_status_t sensor_variant_parse_pressure(const char *word, uint8_t *pressure)
{
  return parse(pressure_table, TABLE_COUNT(pressure_table), word, pressure);
}

sl_status_t sensor_variant_validate(const sensor_variant_t *variant)
{
  if (variant == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  if (is_writable(rtd_table, TABLE_COUNT(rtd_table), variant->rtd)
      && is_writable(pressure_table, TABLE_COUNT(pressure_table),
                     variant->pressure)) {
    return SL_STATUS_OK;
  }

  return SL_STATUS_INVALID_PARAMETER;
}

const char *sensor_variant_rtd_name(uint8_t rtd)
{
  return describe(rtd_table, TABLE_COUNT(rtd_table), rtd);
}

const char *sensor_variant_pressure_name(uint8_t pressure)
{
  return describe(pressure_table, TABLE_COUNT(pressure_table), pressure);
}
