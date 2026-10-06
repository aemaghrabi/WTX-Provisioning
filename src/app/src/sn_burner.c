/***************************************************************************//**
 * @file
 * @brief Writes the device serial number to MCU NVM3 in the provisioning build.
 ******************************************************************************/

#define APP_LOG_TAG  "sn_burner"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app_log.h"
#include "device_sn.h"
#include "nvm_store.h"

#include "sn_burner.h"
#include "xbee_provision.h"

/// @cond INTERNAL
#define SN_BURNER_STR_(x)  #x
#define SN_BURNER_STR(x)   SN_BURNER_STR_(x)
/// @endcond

#if defined(SN_BURNER_SEQUENCE)
/// The NNNNN part this image writes. Stringified, so 00123 stays five digits
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
 * Say what is about to be replaced, if anything.
 ******************************************************************************/
static void report_stored(const char *sn)
{
  char stored[DEVICE_SN_STR_LEN];
  char shown[DEVICE_SN_STR_LEN];
  size_t stored_len = 0U;
  sl_status_t status = nvm_store_read_device_sn(stored, sizeof(stored),
                                                &stored_len);

  switch (status) {
    case SL_STATUS_OK:
      if (memcmp(stored, sn, DEVICE_SN_STR_LEN) == 0) {
        APP_LOG_INFO("the same serial number is already stored; writing it again");
      } else {
        (void)device_sn_to_printable(stored, shown, sizeof(shown));
        APP_LOG_WARNING("overwriting the stored serial number %s", shown);
      }
      break;

    case SL_STATUS_NOT_FOUND:
      break;

    case SL_STATUS_INVALID_COUNT:
      APP_LOG_WARNING("overwriting a stored object of %u bytes, which is not "
                      "a serial number", (unsigned)stored_len);
      break;

    case SL_STATUS_INVALID_TYPE:
      APP_LOG_WARNING("overwriting a counter object stored under the serial "
                      "number key");
      break;

    default:
      // The write below reports whether NVM3 is usable at all.
      APP_LOG_WARNING("could not read the stored serial number, status 0x%04X",
                      (unsigned)status);
      break;
  }
}

/***************************************************************************//**
 * Format the serial number from the build inputs and write it.
 *
 * @return true when it was written and verified.
 ******************************************************************************/
static bool burn(void)
{
  char sn[DEVICE_SN_STR_LEN];
  const char *week_source = "";
  uint32_t sequence = 0U;
  uint16_t yyww = 0U;
  sl_status_t status;

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

  status = device_sn_format(yyww, sequence, sn, sizeof(sn));
  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("serial number not written: it could not be formatted, "
                  "status 0x%04X", (unsigned)status);
    return false;
  }

  APP_LOG_INFO("serial number %s, week %04u from the %s",
               sn, (unsigned)yyww, week_source);

  report_stored(sn);

  status = nvm_store_write_device_sn(sn);
  if (status != SL_STATUS_OK) {
    APP_LOG_ERROR("serial number not written: NVM3 error, status 0x%04X",
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
