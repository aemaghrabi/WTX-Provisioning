/***************************************************************************//**
 * @file
 * @brief Unit tests for the sensor_variant module.
 *
 * The byte values are the contract's: RTD 0x00 or 0x01; pressure 0x00 none,
 * 0x01 1-5VDC, 0x02 Modbus and 0x03 mV bridge (both reserved); 0xFF erased.
 ******************************************************************************/

#include <string.h>

#include "test_util.h"
#include "sensor_variant.h"

/***************************************************************************//**
 * The contract's byte values are the ones in the header.
 ******************************************************************************/
static void test_values(void)
{
  TEST_ASSERT_EQ_UINT(SENSOR_VARIANT_RTD_ABSENT, 0x00U);
  TEST_ASSERT_EQ_UINT(SENSOR_VARIANT_RTD_PRESENT, 0x01U);
  TEST_ASSERT_EQ_UINT(SENSOR_VARIANT_PRESSURE_NONE, 0x00U);
  TEST_ASSERT_EQ_UINT(SENSOR_VARIANT_PRESSURE_1_5VDC, 0x01U);
  TEST_ASSERT_EQ_UINT(SENSOR_VARIANT_PRESSURE_MODBUS, 0x02U);
  TEST_ASSERT_EQ_UINT(SENSOR_VARIANT_PRESSURE_MV_BRIDGE, 0x03U);
  TEST_ASSERT_EQ_UINT(SENSOR_VARIANT_UNSET, 0xFFU);
}

/***************************************************************************//**
 * The RTD words, in any case.
 ******************************************************************************/
static void test_parse_rtd(void)
{
  uint8_t v = 0x55U;

  TEST_ASSERT_EQ_UINT(sensor_variant_parse_rtd("present", &v), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(v, 0x01U);
  TEST_ASSERT_EQ_UINT(sensor_variant_parse_rtd("ABSENT", &v), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(v, 0x00U);
  TEST_ASSERT_EQ_UINT(sensor_variant_parse_rtd("Present", &v), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(v, 0x01U);
}

/***************************************************************************//**
 * Anything else is refused, and the output is left alone.
 ******************************************************************************/
static void test_parse_rtd_rejects(void)
{
  const char *bad[] = { "", "yes", "no", "1", "0", "presen", "presents", "present ",
                        " present", "none" };
  uint8_t v = 0x55U;
  size_t i;

  for (i = 0U; i < (sizeof(bad) / sizeof(bad[0])); i++) {
    TEST_ASSERT_EQ_UINT(sensor_variant_parse_rtd(bad[i], &v), SL_STATUS_INVALID_PARAMETER);
  }
  TEST_ASSERT_EQ_UINT(v, 0x55U);
  TEST_ASSERT_EQ_UINT(sensor_variant_parse_rtd(NULL, &v), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(sensor_variant_parse_rtd("present", NULL), SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * The pressure words, in any case.
 ******************************************************************************/
static void test_parse_pressure(void)
{
  uint8_t v = 0x55U;

  TEST_ASSERT_EQ_UINT(sensor_variant_parse_pressure("none", &v), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(v, 0x00U);
  TEST_ASSERT_EQ_UINT(sensor_variant_parse_pressure("1-5vdc", &v), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(v, 0x01U);
  TEST_ASSERT_EQ_UINT(sensor_variant_parse_pressure("1-5VDC", &v), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(v, 0x01U);
}

/***************************************************************************//**
 * The reserved types are known by name but never accepted for writing.
 ******************************************************************************/
static void test_parse_pressure_rejects(void)
{
  const char *bad[] = { "", "modbus", "Modbus", "mv-bridge", "MV-BRIDGE", "1", "2",
                        "3", "1-5", "1-5vd", "1-5vdc ", "present" };
  uint8_t v = 0x55U;
  size_t i;

  for (i = 0U; i < (sizeof(bad) / sizeof(bad[0])); i++) {
    TEST_ASSERT_EQ_UINT(sensor_variant_parse_pressure(bad[i], &v),
                        SL_STATUS_INVALID_PARAMETER);
  }
  TEST_ASSERT_EQ_UINT(v, 0x55U);
  TEST_ASSERT_EQ_UINT(sensor_variant_parse_pressure(NULL, &v), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(sensor_variant_parse_pressure("none", NULL), SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * Only the four writable combinations validate.
 ******************************************************************************/
static void test_validate(void)
{
  unsigned r;
  unsigned p;

  for (r = 0U; r <= 0xFFU; r++) {
    for (p = 0U; p <= 0xFFU; p++) {
      sensor_variant_t v;
      bool ok = (r <= 1U) && (p <= 1U);

      v.rtd = (uint8_t)r;
      v.pressure = (uint8_t)p;
      if (sensor_variant_validate(&v) != (ok ? SL_STATUS_OK : SL_STATUS_INVALID_PARAMETER)) {
        TEST_FAIL("rtd 0x%02X pressure 0x%02X: wrong verdict", r, p);
      }
    }
  }
  TEST_ASSERT_EQ_UINT(sensor_variant_validate(NULL), SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * Every byte has a name, including the ones never written.
 ******************************************************************************/
static void test_names(void)
{
  unsigned b;

  TEST_ASSERT(strcmp(sensor_variant_rtd_name(0x00U), "absent") == 0);
  TEST_ASSERT(strcmp(sensor_variant_rtd_name(0x01U), "present") == 0);
  TEST_ASSERT(strcmp(sensor_variant_rtd_name(0xFFU), "not set") == 0);
  TEST_ASSERT(strcmp(sensor_variant_rtd_name(0x02U), "invalid") == 0);

  TEST_ASSERT(strcmp(sensor_variant_pressure_name(0x00U), "none") == 0);
  TEST_ASSERT(strcmp(sensor_variant_pressure_name(0x01U), "1-5VDC") == 0);
  TEST_ASSERT(strcmp(sensor_variant_pressure_name(0x02U), "Modbus (reserved)") == 0);
  TEST_ASSERT(strcmp(sensor_variant_pressure_name(0x03U), "mV bridge (reserved)") == 0);
  TEST_ASSERT(strcmp(sensor_variant_pressure_name(0xFFU), "not set") == 0);
  TEST_ASSERT(strcmp(sensor_variant_pressure_name(0x04U), "invalid") == 0);

  for (b = 0U; b <= 0xFFU; b++) {
    TEST_ASSERT(sensor_variant_rtd_name((uint8_t)b) != NULL);
    TEST_ASSERT(sensor_variant_pressure_name((uint8_t)b) != NULL);
  }
}

int main(void)
{
  TEST_RUN(test_values);
  TEST_RUN(test_parse_rtd);
  TEST_RUN(test_parse_rtd_rejects);
  TEST_RUN(test_parse_pressure);
  TEST_RUN(test_parse_pressure_rejects);
  TEST_RUN(test_validate);
  TEST_RUN(test_names);
  return TEST_SUMMARY();
}
