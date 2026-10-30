/***************************************************************************//**
 * @file
 * @brief Writes the device serial number to USERDATA in the provisioning build.
 ******************************************************************************/

#define APP_LOG_TAG  "sn_burner"

#include <stdbool.h>
#include <stdint.h>

#include "app_log.h"
#include "device_sn.h"
#include "mfg_store.h"
#include "sensor_variant.h"

#include "sn_burner.h"
#include "xbee_provision.h"

/// @cond INTERNAL
#define SN_BURNER_STR_(x)  #x
#define SN_BURNER_STR(x)   SN_BURNER_STR_(x)
/// @endcond

#if defined(SN_BURNER_FAMILY)
/// The FF part this image writes. Stringified, so 07 is never read as octal.
static const char family_text[] = SN_BURNER_STR(SN_BURNER_FAMILY);

_Static_assert(sizeof(family_text) == (DEVICE_SN_FAMILY_DIGITS + 1U),
               "SN_BURNER_FAMILY must be exactly two digits, for example 18");
#else
/// No family code in this image. Only the provisioning build calls this
/// module, and app.c refuses to build that one without it.
static const char family_text[] = "";
#endif

#if defined(SN_BURNER_SEQUENCE)
/// The SSSSS part this image writes. Stringified, so 00123 stays five digits
/// rather than becoming the octal constant 83.
static const char sequence_text[] = SN_BURNER_STR(SN_BURNER_SEQUENCE);

_Static_assert(sizeof(sequence_text) == (DEVICE_SN_SEQUENCE_DIGITS + 1U),
               "SN_BURNER_SEQUENCE must be exactly five digits, for example 00123");
#else
/// No sequence number in this image. Only the provisioning build calls this
/// module, and app.c refuses to build that one without it.
static const char sequence_text[] = "";
#endif

#if defined(SN_BURNER_BUILD_YYWW)
/// The YYWW part from the build host's date.
static const char build_yyww_text[] = SN_BURNER_STR(SN_BURNER_BUILD_YYWW);

_Static_assert(sizeof(build_yyww_text) == (DEVICE_SN_YYWW_DIGITS + 1U),
               "SN_BURNER_BUILD_YYWW must be exactly four digits, for example 2641");
#endif

#if defined(SN_BURNER_RTD) && defined(SN_BURNER_PRESSURE)
_Static_assert(((SN_BURNER_RTD) == SENSOR_VARIANT_RTD_ABSENT)
               || ((SN_BURNER_RTD) == SENSOR_VARIANT_RTD_PRESENT),
               "SN_BURNER_RTD must be 0 (absent) or 1 (present)");
_Static_assert(((SN_BURNER_PRESSURE) == SENSOR_VARIANT_PRESSURE_NONE)
               || ((SN_BURNER_PRESSURE) == SENSOR_VARIANT_PRESSURE_1_5VDC),
               "SN_BURNER_PRESSURE must be 0 (none) or 1 (1-5VDC); Modbus and "
               "mV bridge are reserved and are never written");

/// The sensor variant this image writes.
static const sensor_variant_t build_variant = {
  (uint8_t)(SN_BURNER_RTD),
  (uint8_t)(SN_BURNER_PRESSURE),
};
#else
/// No sensor variant in this image. Only the provisioning build calls this
/// module, and app.c refuses to build that one without it. Erased bytes fail
/// sensor_variant_validate(), so nothing would be written.
static const sensor_variant_t build_variant = {
  SENSOR_VARIANT_UNSET,
  SENSOR_VARIANT_UNSET,
};
#endif

/// True once the step has run, either way.
static bool finished;

/// True once the serial number has been written and verified.
static bool written;

/***************************************************************************//**
 * Work out the build week, and say where it came from.
 ******************************************************************************/
static sl_status_t build_week(uint16_t *yyww, const char **source)
{
#if defined(SN_BURNER_BUILD_YYWW)
  *source = "build host date";
  return device_sn_parse_yyww(build_yyww_text, yyww);
#else
  *source = "compiler build date";
  return device_sn_yyww_from_build_date(__DATE__, yyww);
#endif
}

/***************************************************************************//**
 * Say what the USERDATA page holds before it is erased.
 ******************************************************************************/
static void report_before_erase(void)
{
  sensor_variant_t held;

  char stored[DEVICE_SN_STR_SIZE];
  char shown[DEVICE_SN_STR_SIZE];
  bool blank = false;
  sl_status_t status = mfg_store_read_device_sn(stored, sizeof(stored));

  if ((status == SL_STATUS_OK) || (status == SL_STATUS_INVALID_COUNT)) {
    (void)device_sn_to_printable(stored, shown, sizeof(shown));
    APP_LOG_INFO("erasing USERDATA (it held serial number %s)", shown);
  } else if ((mfg_store_is_blank(&blank) == SL_STATUS_OK) && blank) {
    APP_LOG_INFO("erasing USERDATA (blank)");
  } else {
    APP_LOG_INFO("erasing USERDATA (it held no serial number, but was not "
                 "blank)");
  }

  if ((mfg_store_read_sensor_variant(&held) == SL_STATUS_OK)
      && ((held.rtd != SENSOR_VARIANT_UNSET)
          || (held.pressure != SENSOR_VARIANT_UNSET))) {
    APP_LOG_INFO("the erased sensor variant was RTD %s (0x%02X), pressure %s "
                 "(0x%02X)", sensor_variant_rtd_name(held.rtd),
                 (unsigned)held.rtd, sensor_variant_pressure_name(held.pressure),
                 (unsigned)held.pressure);
  }
}

/***************************************************************************//**
 * Format the serial number from the build inputs and write it.
 *
 * @return true when it was written and verified.
 ******************************************************************************/
static bool burn(void)
{
  char sn[DEVICE_SN_STR_SIZE];
  const char *week_source = "";
  uint32_t sequence = 0U;
  uint16_t yyww = 0U;
  uint8_t family = 0U;
  sl_status_t status;

  if (device_sn_parse_family(family_text, &family) != SL_STATUS_OK) {
    APP_LOG_ERROR("serial number not written: the family code built into "
                  "this image, \"%s\", is not two digits", family_text);
    return false;
  }

  if (device_sn_parse_sequence(sequence_text, &sequence) != SL_STATUS_OK) {
    APP_LOG_ERROR("serial number not written: the sequence number built into "
                  "this image, \"%s\", is not five digits", sequence_text);
    return false;
  }

  status = build_week(&yyww, &week_source);
  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("serial number not written: no valid week from the %s, "
                  "status 0x%04X", week_source, (unsigned)status);
    return false;
  }

  status = device_sn_format(family, yyww, sequence, sn, sizeof(sn));
  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("serial number not written: it could not be formatted, "
                  "status 0x%04X", (unsigned)status);
    return false;
  }

  APP_LOG_INFO("serial number %s, week %04u from the %s",
               sn, (unsigned)yyww, week_source);

  // Checked before anything is erased, so that an image with a variant that
  // cannot be written leaves the unit as it was.
  if (sensor_variant_validate(&build_variant) != SL_STATUS_OK) {
    APP_LOG_ERROR("serial number not written: the sensor variant built into "
                  "this image (rtd=0x%02X pressure=0x%02X) is not one that may "
                  "be written", (unsigned)build_variant.rtd,
                  (unsigned)build_variant.pressure);
    return false;
  }

  APP_LOG_INFO("sensor variant RTD %s (0x%02X), pressure %s (0x%02X)",
               sensor_variant_rtd_name(build_variant.rtd),
               (unsigned)build_variant.rtd,
               sensor_variant_pressure_name(build_variant.pressure),
               (unsigned)build_variant.pressure);

  // Flash cannot be rewritten in place, so every run starts from an erased
  // page. Flash is erased a page at a time: everything in USERDATA goes.
  report_before_erase();
  status = mfg_store_erase();
  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("serial number not written: erasing USERDATA failed, "
                  "status 0x%04X", (unsigned)status);
    return false;
  }

  // The variant goes in before the serial number, which is the last thing
  // written: a unit that has a serial number therefore has its variant too.
  status = mfg_store_write_sensor_variant(&build_variant);
  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("serial number not written: the sensor variant could not be "
                  "written, status 0x%04X", (unsigned)status);
    return false;
  }

  APP_LOG_INFO("sensor variant written: rtd=0x%02X pressure=0x%02X",
               (unsigned)build_variant.rtd, (unsigned)build_variant.pressure);

  status = mfg_store_write_device_sn(sn);
  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("serial number not written: USERDATA error, status 0x%04X",
                  (unsigned)status);
    return false;
  }

  APP_LOG_INFO("serial number written: %s", sn);

  return true;
}

/***************************************************************************//**
 * Advance the serial number step.
 ******************************************************************************/
void sn_burner_process(void)
{
  if (finished || !xbee_provision_is_finished()) {
    return;
  }

  if (xbee_provision_passed()) {
    written = burn();
  } else {
    APP_LOG_ERROR("serial number not written: XBee provisioning did not pass (%s)",
                  xbee_provision_result_str(xbee_provision_get_result()));
    written = false;
  }

  finished = true;
}

/***************************************************************************//**
 * Report whether the serial number step has finished.
 ******************************************************************************/
bool sn_burner_is_finished(void)
{
  return finished;
}

/***************************************************************************//**
 * Report whether the serial number was written and verified.
 ******************************************************************************/
bool sn_burner_passed(void)
{
  return written;
}
