/***************************************************************************//**
 * @file
 * @brief Mode-independent facade over the XBee module.
 ******************************************************************************/

#define APP_LOG_TAG  "xbee"

#include <string.h>

#include "sl_sleeptimer.h"

#include "app_log.h"
#include "byte_util.h"
#include "xbee_power.h"
#include "xbee_reset.h"
#include "xbee_sleep.h"
#include "xbee_uart.h"
#include "xbee.h"

/// Parameters bring-up reads once the mode is known, in the order it reads them.
static const uint16_t bringup_reads[] = {
  XBEE_AT_AO, XBEE_AT_NP, XBEE_AT_VR, XBEE_AT_HV,
  XBEE_AT_SH, XBEE_AT_SL, XBEE_AT_GT, XBEE_AT_CC, XBEE_AT_CT,
  XBEE_AT_SM, XBEE_AT_D8, XBEE_AT_D9,
};

/// Number of parameters bring-up reads.
#define BRINGUP_READ_COUNT  ((uint16_t)(sizeof(bringup_reads) / sizeof(bringup_reads[0])))

/// Sleep mode values that make the sleep request line meaningful
/// (manual lines 4703 to 4757).
#define SM_PIN_SLEEP        1U
#define SM_CYCLIC_PIN_WAKE  5U

/// Pin configuration value that routes a sleep pin to its peripheral function.
#define PIN_FUNCTION_ENABLED  1U

/// Bring-up state.
static xbee_state_t state = XBEE_STATE_OFF;

/// Why bring-up stopped, when it did.
static sl_status_t failure_result = SL_STATUS_OK;

/// How the facade was started.
static xbee_config_t cfg;

/// Mode detected, or requested when it was not AUTO.
static xbee_mode_t detected_mode = XBEE_MODE_AUTO;

/// Parameters read during bring-up.
static xbee_info_t info;

/// Request the state machine uses for its own reads.
static xbee_at_req_t internal_req;

/// Index into bringup_reads while the parameters are being read.
static uint16_t read_index;

/// Timeout applied to the request being dispatched, in milliseconds.
///
/// Probe steps use the configured probe timeout; everything else uses the
/// transport's own default.
static uint32_t dispatch_timeout_ms;

/// The application's request, while one is in flight.
static xbee_at_req_t *user_req;

/// Deadline for the current state, where it has one.
static uint32_t state_deadline_tick;

/// Sleep values read from the module, used to decide whether pin sleep works.
static uint8_t module_sm;
static uint8_t module_d8;
static uint8_t module_d9;

/// True once the status line has confirmed the module is asleep.
static bool sleep_confirmed;

/// True while the sleep request line is asserted.
static bool sleep_requested;

/// Tick until which the module is still settling after a wake.
static uint32_t wake_guard_tick;

/// Mode a pending xbee_set_mode() is switching to, or XBEE_MODE_AUTO for none.
static xbee_mode_t pending_mode = XBEE_MODE_AUTO;

/// The request carrying that mode change.
static const xbee_at_req_t *pending_mode_req;

/// Application callbacks.
static xbee_data_cb_t data_cb;
static void *data_user;
static xbee_frame_cb_t frame_cb;
static void *frame_user;
static xbee_modem_status_cb_t modem_cb;
static void *modem_user;

static sl_status_t start_request(xbee_at_req_t *request,
                                 uint16_t command,
                                 xbee_op_t op,
                                 const uint8_t *value,
                                 uint16_t len,
                                 uint32_t timeout_ms);

/***************************************************************************//**
 * Report whether a deadline has been reached, tolerating tick counter wrap.
 ******************************************************************************/
static bool tick_reached(uint32_t deadline)
{
  return ((int32_t)(sl_sleeptimer_get_tick_count() - deadline) >= 0);
}

/***************************************************************************//**
 * Convert a millisecond interval into an absolute tick deadline from now.
 ******************************************************************************/
static uint32_t deadline_from_ms(uint32_t ms)
{
  uint32_t ticks = 0U;

  if (sl_sleeptimer_ms32_to_tick(ms, &ticks) != SL_STATUS_OK) {
    ticks = sl_sleeptimer_ms_to_tick((uint16_t)UINT16_MAX);
  }

  return sl_sleeptimer_get_tick_count() + ticks;
}

/***************************************************************************//**
 * Report whether the module is currently reached through API frames.
 ******************************************************************************/
static bool mode_is_api(xbee_mode_t mode)
{
  return ((mode == XBEE_MODE_API1) || (mode == XBEE_MODE_API2));
}

/***************************************************************************//**
 * Name of a serial mode, for the log.
 ******************************************************************************/
static const char *mode_name(xbee_mode_t mode)
{
  const char *name;

  switch (mode) {
    case XBEE_MODE_TRANSPARENT: name = "transparent"; break;
    case XBEE_MODE_API1:        name = "API 1, unescaped"; break;
    case XBEE_MODE_API2:        name = "API 2, escaped"; break;
    default:                    name = "undetermined"; break;
  }

  return name;
}

/***************************************************************************//**
 * Stop bring-up with a reason.
 ******************************************************************************/
static void fail(sl_status_t result)
{
  APP_LOG_ERROR("bring-up failed in state %u, status 0x%04X",
                (unsigned)state, (unsigned)result);
  failure_result = result;
  state = XBEE_STATE_FAILED;
}

/***************************************************************************//**
 * Deliver received data to the application, whatever carried it.
 ******************************************************************************/
static void deliver_data(uint64_t addr64,
                         uint16_t addr16,
                         uint8_t options,
                         const uint8_t *payload,
                         uint16_t len)
{
  if ((data_cb != NULL) && (len > 0U)) {
    data_cb(addr64, addr16, options, payload, len, data_user);
  }
}

/***************************************************************************//**
 * Unsolicited API frame: normalise the receive families and pass on the rest.
 ******************************************************************************/
static void on_api_frame(const xbee_frame_t *frame, void *user)
{
  (void)user;

  if (frame_cb != NULL) {
    frame_cb(frame, frame_user);
  }

  switch (frame->type) {
    case XBEE_FRAME_RX:
      // Modern receive frame, emitted when AO is 0.
      deliver_data(frame->u.rx.source_addr64, frame->u.rx.source_addr16,
                   frame->u.rx.options, frame->u.rx.data, frame->u.rx.data_len);
      break;

    case XBEE_FRAME_RX64:
      // Legacy receive frame, emitted when AO is 2, which is the default.
      deliver_data(frame->u.rx64.source_addr64, XBEE_ADDR16_UNKNOWN,
                   frame->u.rx64.options, frame->u.rx64.data,
                   frame->u.rx64.data_len);
      break;

    case XBEE_FRAME_RX16:
      deliver_data(XBEE_ADDR64_UNKNOWN, frame->u.rx16.source_addr16,
                   frame->u.rx16.options, frame->u.rx16.data,
                   frame->u.rx16.data_len);
      break;

    case XBEE_FRAME_EXPLICIT_RX:
      deliver_data(frame->u.explicit_rx.source_addr64, XBEE_ADDR16_UNKNOWN,
                   frame->u.explicit_rx.options, frame->u.explicit_rx.data,
                   frame->u.explicit_rx.data_len);
      break;

    case XBEE_FRAME_MODEM_STATUS:
      if (modem_cb != NULL) {
        modem_cb(frame->u.modem_status.status, modem_user);
      }
      break;

    default:
      // I/O samples, relay output and undocumented types reach the frame
      // callback only.
      break;
  }
}

/***************************************************************************//**
 * Transparent mode data: no addressing is available in that mode.
 ******************************************************************************/
static void on_transparent_data(const uint8_t *payload, uint16_t len, void *user)
{
  (void)user;

  deliver_data(XBEE_ADDR64_UNKNOWN, XBEE_ADDR16_UNKNOWN, 0U, payload, len);
}

/***************************************************************************//**
 * Point the local transports at a mode and remember it.
 ******************************************************************************/
static sl_status_t adopt_mode(xbee_mode_t mode)
{
  sl_status_t status = SL_STATUS_OK;

  detected_mode = mode;

  if (mode_is_api(mode)) {
    status = xbee_api_set_escaped(mode == XBEE_MODE_API2);
  }

  return status;
}

/***************************************************************************//**
 * Complete the application's request and release the slot.
 ******************************************************************************/
static void finish_user_request(xbee_at_req_t *request, sl_status_t result)
{
  request->result = result;
  request->state = (result == SL_STATUS_OK) ? XBEE_REQ_DONE : XBEE_REQ_FAILED;

  if (user_req == request) {
    user_req = NULL;
  }
}

/***************************************************************************//**
 * Translate the module's AT command status into a status code.
 ******************************************************************************/
static sl_status_t status_from_at(uint8_t at_status)
{
  sl_status_t result;

  switch (at_status) {
    case XBEE_AT_STATUS_OK:
      result = SL_STATUS_OK;
      break;
    case XBEE_AT_STATUS_INVALID_COMMAND:
      result = SL_STATUS_NOT_FOUND;
      break;
    case XBEE_AT_STATUS_INVALID_PARAMETER:
      result = SL_STATUS_INVALID_PARAMETER;
      break;
    default:
      result = SL_STATUS_FAIL;
      break;
  }

  return result;
}

/***************************************************************************//**
 * Copy a value into a request, bounded by the request's own buffer.
 ******************************************************************************/
static sl_status_t store_value(xbee_at_req_t *request,
                               const uint8_t *value,
                               uint16_t len)
{
  if (len > (uint16_t)sizeof(request->value)) {
    // Truncating would hand back a silently wrong value.
    return SL_STATUS_WOULD_OVERFLOW;
  }

  if (len > 0U) {
    (void)memcpy(request->value, value, len);
  }
  request->value_len = len;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Send a prepared request through the API transport.
 ******************************************************************************/
static sl_status_t send_via_api(xbee_at_req_t *request)
{
  xbee_frame_t frame;

  if (request->op == XBEE_OP_QUEUE_SET) {
    xbee_frame_at_queue_defaults(&frame);
  } else {
    xbee_frame_at_defaults(&frame);
  }

  frame.u.at.command = request->command;
  if (request->value_in_len > 0U) {
    frame.u.at.value = request->value_in;
    frame.u.at.value_len = request->value_in_len;
  }

  // A discovery answers with one frame per node and ends on its own timeout, so
  // it needs the collecting form and a longer window.
  {
    const xbee_at_entry_t *entry = xbee_at_table_find(request->command);

    if ((entry != NULL) && ((entry->flags & XBEE_AT_FLAG_MULTI_RESPONSE) != 0U)) {
      return xbee_api_send_multi(&frame, &request->t.api,
                                 XBEE_CMD_MODE_MULTILINE_TIMEOUT_MS,
                                 NULL, NULL);
    }
  }

  return xbee_api_send(&frame, &request->t.api, dispatch_timeout_ms);
}

/***************************************************************************//**
 * Send a prepared request through the Command mode transport.
 ******************************************************************************/
static sl_status_t send_via_cmd_mode(xbee_at_req_t *request)
{
  const xbee_at_entry_t *entry = xbee_at_table_find(request->command);
  xbee_cmd_value_fmt_t fmt = XBEE_CMD_VALUE_NONE;
  bool multi = false;

  if (request->value_in_len > 0U) {
    // A string parameter is written as it stands; everything else goes out as
    // hexadecimal (manual lines 3080 to 3096).
    fmt = ((entry != NULL)
           && ((entry->type == XBEE_AT_TYPE_STRING)
               || (entry->type == XBEE_AT_TYPE_SUBCOMMAND)))
          ? XBEE_CMD_VALUE_TEXT : XBEE_CMD_VALUE_HEX;
  }

  if ((entry != NULL) && ((entry->flags & XBEE_AT_FLAG_MULTI_RESPONSE) != 0U)) {
    multi = true;
  }

  if (multi) {
    return xbee_cmd_mode_send_multi(request->command,
                                    request->value_in, request->value_in_len,
                                    fmt, &request->t.cmd,
                                    XBEE_CMD_MODE_MULTILINE_TIMEOUT_MS,
                                    NULL, NULL);
  }

  return xbee_cmd_mode_send(request->command,
                            request->value_in, request->value_in_len,
                            fmt, &request->t.cmd, dispatch_timeout_ms);
}

/***************************************************************************//**
 * Hand a prepared request to whichever transport is in use.
 ******************************************************************************/
static sl_status_t dispatch_request(xbee_at_req_t *request)
{
  if (mode_is_api(detected_mode)) {
    return send_via_api(request);
  }

  if (!xbee_cmd_mode_is_ready()) {
    // The session is opened on demand; xbee_process() sends the command once
    // the module answers.
    request->awaiting_session = true;
    return xbee_cmd_mode_enter();
  }

  request->awaiting_session = false;
  return send_via_cmd_mode(request);
}

/***************************************************************************//**
 * Advance a request that is in flight.
 ******************************************************************************/
static void poll_request(xbee_at_req_t *request)
{
  if (request->awaiting_session) {
    if (xbee_cmd_mode_is_ready()) {
      sl_status_t status;

      request->awaiting_session = false;
      status = send_via_cmd_mode(request);
      if (status != SL_STATUS_OK) {
        finish_user_request(request, status);
      }
    } else if (xbee_cmd_mode_get_state() == XBEE_CMD_STATE_IDLE) {
      // Entry gave up: the module is not answering the escape sequence.
      finish_user_request(request, SL_STATUS_TIMEOUT);
    } else {
      // Entry is still running.
    }
    return;
  }

  if (mode_is_api(detected_mode)) {
    const xbee_api_request_t *api = &request->t.api;

    if (!xbee_api_request_complete(api)) {
      return;
    }
    if (api->result != SL_STATUS_OK) {
      finish_user_request(request, api->result);
      return;
    }

    request->status = (uint8_t)api->response.u.at_response.status;
    if (store_value(request,
                    api->response.u.at_response.value,
                    api->response.u.at_response.value_len) != SL_STATUS_OK) {
      finish_user_request(request, SL_STATUS_WOULD_OVERFLOW);
      return;
    }
    finish_user_request(request, status_from_at(request->status));
    return;
  }

  {
    const xbee_cmd_request_t *cmd = &request->t.cmd;
    uint16_t len = 0U;

    if (!xbee_cmd_mode_request_complete(cmd)) {
      return;
    }
    if (cmd->result != SL_STATUS_OK) {
      // Command mode reports failure as ERROR, with no numeric code of its own
      // (manual lines 3097 to 3099).
      request->status = (uint8_t)((cmd->reply == XBEE_CMD_LINE_ERROR)
                                  ? XBEE_AT_STATUS_ERROR : XBEE_AT_STATUS_OK);
      finish_user_request(request, cmd->result);
      return;
    }

    request->status = (uint8_t)XBEE_AT_STATUS_OK;

    if (cmd->reply == XBEE_CMD_LINE_VALUE) {
      const xbee_at_entry_t *entry = xbee_at_table_find(request->command);
      bool as_text = ((entry != NULL)
                      && ((entry->type == XBEE_AT_TYPE_STRING)
                          || (entry->type == XBEE_AT_TYPE_SUBCOMMAND)));

      if (as_text) {
        // A string parameter comes back as characters, not as hexadecimal.
        if (store_value(request, (const uint8_t *)cmd->value, cmd->value_len)
            != SL_STATUS_OK) {
          finish_user_request(request, SL_STATUS_WOULD_OVERFLOW);
          return;
        }
      } else if (xbee_cmd_mode_value_bytes(cmd, request->value,
                                           (uint16_t)sizeof(request->value),
                                           &len) == SL_STATUS_OK) {
        request->value_len = len;
      } else {
        finish_user_request(request, SL_STATUS_INVALID_PARAMETER);
        return;
      }
    }

    finish_user_request(request, SL_STATUS_OK);
  }
}

/***************************************************************************//**
 * Record a parameter that bring-up has just read.
 ******************************************************************************/
static void record_parameter(uint16_t command, const xbee_at_req_t *request)
{
  uint64_t value = 0U;

  if (byte_util_be_to_u64(request->value, request->value_len, &value)
      != SL_STATUS_OK) {
    return;
  }

  switch (command) {
    case XBEE_AT_AO: info.ao = (uint8_t)value; break;
    case XBEE_AT_NP: info.np = (uint16_t)value; break;
    case XBEE_AT_VR: info.vr = (uint16_t)value; break;
    case XBEE_AT_HV: info.hv = (uint16_t)value; break;
    case XBEE_AT_SH: info.serial_high = (uint32_t)value; break;
    case XBEE_AT_SL: info.serial_low = (uint32_t)value; break;
    case XBEE_AT_GT: info.gt = (uint16_t)value; break;
    case XBEE_AT_CC: info.cc = (uint8_t)value; break;
    case XBEE_AT_CT: info.ct = (uint16_t)value; break;
    case XBEE_AT_SM: module_sm = (uint8_t)value; break;
    case XBEE_AT_D8: module_d8 = (uint8_t)value; break;
    case XBEE_AT_D9: module_d9 = (uint8_t)value; break;
    default: break;
  }
}

/***************************************************************************//**
 * Start reading the parameters bring-up needs.
 ******************************************************************************/
static void begin_read_params(void)
{
  read_index = 0U;
  state = XBEE_STATE_READ_PARAMS;

  if (start_request(&internal_req, bringup_reads[0], XBEE_OP_GET, NULL, 0U,
                    cfg.probe_timeout_ms) != SL_STATUS_OK) {
    fail(SL_STATUS_FAIL);
  }
}

/***************************************************************************//**
 * Begin probing which mode the module is in.
 ******************************************************************************/
static void begin_probe(void)
{
  if (cfg.mode == XBEE_MODE_TRANSPARENT) {
    detected_mode = XBEE_MODE_TRANSPARENT;
    state = XBEE_STATE_PROBE_CMD;
    if (xbee_cmd_mode_enter() != SL_STATUS_OK) {
      fail(SL_STATUS_FAIL);
    }
    return;
  }

  if (mode_is_api(cfg.mode)) {
    if (adopt_mode(cfg.mode) != SL_STATUS_OK) {
      fail(SL_STATUS_FAIL);
      return;
    }
    begin_read_params();
    return;
  }

  // Detecting. An AP query contains no byte that needs escaping, in either
  // direction, so one unescaped frame is understood whether the module is in
  // API mode 1 or API mode 2 (manual lines 7188 to 7245). Its answer says which.
  APP_LOG_INFO("probing: asking for AP with an API frame");
  detected_mode = XBEE_MODE_API1;
  state = XBEE_STATE_PROBE_API;
  if (start_request(&internal_req, XBEE_AT_AP, XBEE_OP_GET, NULL, 0U,
                    cfg.probe_timeout_ms) != SL_STATUS_OK) {
    fail(SL_STATUS_FAIL);
  }
}

/***************************************************************************//**
 * Interpret the AP value the probe obtained.
 ******************************************************************************/
static void adopt_probed_ap(uint8_t ap)
{
  info.ap = ap;

  switch (ap) {
    case 0U:
      (void)adopt_mode(XBEE_MODE_TRANSPARENT);
      break;
    case 1U:
      (void)adopt_mode(XBEE_MODE_API1);
      break;
    case 2U:
      (void)adopt_mode(XBEE_MODE_API2);
      break;
    default:
      // AP=4 is API inside MicroPython, which this stack does not drive.
      APP_LOG_ERROR("module reports AP=%u, which this stack does not drive",
                    (unsigned)ap);
      fail(SL_STATUS_NOT_SUPPORTED);
      return;
  }

  APP_LOG_INFO("detected %s mode, AP=%u", mode_name(detected_mode), (unsigned)ap);
  begin_read_params();
}

/***************************************************************************//**
 * Advance the bring-up state machine.
 ******************************************************************************/
static void process_bringup(void)
{
  switch (state) {
    case XBEE_STATE_RESETTING:
      if (xbee_reset_pulse_process() == SL_STATUS_IN_PROGRESS) {
        return;
      }
      state = XBEE_STATE_SETTLING;
      state_deadline_tick = deadline_from_ms(cfg.settle_ms);
      break;

    case XBEE_STATE_SETTLING:
      if (!tick_reached(state_deadline_tick)) {
        return;
      }
      // Anything the module emitted while booting, such as its own status
      // frame, is not a reply to anything and would only confuse the probe.
      (void)xbee_uart_rx_flush();
      begin_probe();
      break;

    case XBEE_STATE_PROBE_API:
      poll_request(&internal_req);
      if (!xbee_at_req_complete(&internal_req)) {
        return;
      }
      if (internal_req.result == SL_STATUS_OK) {
        uint64_t ap = 0U;

        (void)byte_util_be_to_u64(internal_req.value,
                                  internal_req.value_len, &ap);
        adopt_probed_ap((uint8_t)ap);
      } else {
        // No answer in API mode. Try the escape sequence instead.
        APP_LOG_INFO("no API response, trying the command mode escape sequence");
        detected_mode = XBEE_MODE_TRANSPARENT;
        state = XBEE_STATE_PROBE_CMD;
        (void)xbee_uart_rx_flush();
        if (xbee_cmd_mode_enter() != SL_STATUS_OK) {
          fail(SL_STATUS_FAIL);
        }
      }
      break;

    case XBEE_STATE_PROBE_CMD:
      if (xbee_cmd_mode_is_ready()) {
        APP_LOG_INFO("command mode entered, reading AP");
        state = XBEE_STATE_PROBE_CMD_READ;
        if (start_request(&internal_req, XBEE_AT_AP, XBEE_OP_GET, NULL, 0U,
                          cfg.probe_timeout_ms) != SL_STATUS_OK) {
          fail(SL_STATUS_FAIL);
        }
      } else if (xbee_cmd_mode_get_state() == XBEE_CMD_STATE_IDLE) {
        // Neither API frames nor the escape sequence were answered.
        APP_LOG_ERROR("no response to the escape sequence either");
        fail(SL_STATUS_NO_MORE_RESOURCE);
      } else {
        // Entry is still running.
      }
      break;

    case XBEE_STATE_PROBE_CMD_READ:
      poll_request(&internal_req);
      if (!xbee_at_req_complete(&internal_req)) {
        return;
      }
      if (internal_req.result != SL_STATUS_OK) {
        fail(internal_req.result);
        return;
      }
      {
        uint64_t ap = 0U;

        (void)byte_util_be_to_u64(internal_req.value,
                                  internal_req.value_len, &ap);
        if (ap != 0U) {
          // The module says it is in an API mode, yet it did not answer an API
          // frame. Report that rather than guess which mode to use.
          APP_LOG_ERROR("module reports AP=%u but did not answer an API frame",
                        (unsigned)ap);
          info.ap = (uint8_t)ap;
          fail(SL_STATUS_INVALID_STATE);
          return;
        }
        adopt_probed_ap(0U);
      }
      break;

    case XBEE_STATE_READ_PARAMS:
      poll_request(&internal_req);
      if (!xbee_at_req_complete(&internal_req)) {
        return;
      }
      if (internal_req.result == SL_STATUS_OK) {
        record_parameter(bringup_reads[read_index], &internal_req);
      } else {
        char name[4];

        // A parameter the module will not report is left at its default rather
        // than failing bring-up: the value may simply not apply to this variant.
        (void)xbee_at_id_to_str(bringup_reads[read_index], name, sizeof(name));
        APP_LOG_WARNING("could not read %s, status 0x%04X, using the default",
                        name, (unsigned)internal_req.result);
      }
      read_index++;
      if (read_index < BRINGUP_READ_COUNT) {
        if (start_request(&internal_req, bringup_reads[read_index],
                          XBEE_OP_GET, NULL, 0U,
                          cfg.probe_timeout_ms) != SL_STATUS_OK) {
          fail(SL_STATUS_FAIL);
        }
        return;
      }

      // Adopt the module's own Command mode parameters, so a module configured
      // away from the defaults stays reachable.
      if (detected_mode == XBEE_MODE_TRANSPARENT) {
        (void)xbee_cmd_mode_exit(NULL);
      }
      if ((info.gt > 0U) && (info.ct > 0U)) {
        // CT is counted in units of 100 ms (manual lines 6157 to 6164).
        (void)xbee_cmd_mode_set_params((char)info.cc, info.gt,
                                       (uint32_t)info.ct * 100U);
      }
      info.valid = true;
      APP_LOG_INFO("ready in %s mode", mode_name(detected_mode));
      state = XBEE_STATE_READY;
      break;

    default:
      break;
  }
}

/***************************************************************************//**
 * Return to the ready state after a wake, applying the settling period.
 ******************************************************************************/
static void wake_to_ready(sl_status_t result)
{
  sleep_confirmed = false;
  failure_result = result;
  wake_guard_tick = deadline_from_ms(XBEE_SLEEP_WAKE_GUARD_MS);
  state = XBEE_STATE_READY;
}

/***************************************************************************//**
 * Advance a sleep transition by watching the module's status line.
 ******************************************************************************/
static void process_sleep(void)
{
  bool awake = true;

  if (xbee_sleep_is_awake(&awake) != SL_STATUS_OK) {
    return;
  }

  if (sleep_requested) {
    if (!awake) {
      // The module finished what it was doing and went to sleep.
      sleep_confirmed = true;
      return;
    }
    if (sleep_confirmed) {
      // It woke by itself, for instance at the end of a cyclic sleep period.
      wake_to_ready(SL_STATUS_OK);
      return;
    }
    if (tick_reached(state_deadline_tick)) {
      // It never slept. Release the line and report that, rather than leave the
      // caller believing the module is asleep.
      (void)xbee_sleep_request(false);
      sleep_requested = false;
      wake_to_ready(SL_STATUS_TIMEOUT);
    }
    return;
  }

  // Waking: the status line going high is the only readiness signal available,
  // because CTS is not wired on this board.
  if (awake) {
    wake_to_ready(SL_STATUS_OK);
    return;
  }
  if (tick_reached(state_deadline_tick)) {
    // The line never rose. The caller finds out through the next request, which
    // will fail against a module that is still asleep.
    wake_to_ready(SL_STATUS_TIMEOUT);
  }
}

/***************************************************************************//**
 * Prepare and dispatch a request.
 ******************************************************************************/
static sl_status_t start_request(xbee_at_req_t *request,
                                 uint16_t command,
                                 xbee_op_t op,
                                 const uint8_t *value,
                                 uint16_t len,
                                 uint32_t timeout_ms)
{
  sl_status_t status;

  dispatch_timeout_ms = timeout_ms;

  if (len > (uint16_t)sizeof(request->value_in)) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  (void)memset(request, 0, sizeof(*request));
  request->state = XBEE_REQ_PENDING;
  request->result = SL_STATUS_IN_PROGRESS;
  request->command = command;
  request->op = (uint8_t)op;
  if (len > 0U) {
    (void)memcpy(request->value_in, value, len);
    request->value_in_len = len;
  }

  status = dispatch_request(request);
  if (status != SL_STATUS_OK) {
    request->state = XBEE_REQ_FAILED;
    request->result = status;
  }

  return status;
}

/***************************************************************************//**
 * Accept an application request: check it, then dispatch it.
 ******************************************************************************/
static sl_status_t submit(uint16_t command,
                          xbee_op_t op,
                          const uint8_t *value,
                          uint16_t len,
                          xbee_at_req_t *request)
{
  sl_status_t status;

  if (request == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if ((value == NULL) && (len > 0U)) {
    return SL_STATUS_NULL_POINTER;
  }
  if (state != XBEE_STATE_READY) {
    return SL_STATUS_NOT_READY;
  }
  if (!tick_reached(wake_guard_tick)) {
    // Just woken. The manual requires a short settling period before the module
    // is ready to receive (lines 4726 to 4729).
    return SL_STATUS_NOT_READY;
  }
  if (user_req != NULL) {
    return SL_STATUS_BUSY;
  }

  // The command table knows what the module will accept, so an impossible
  // request is refused before it reaches the serial link.
  status = (op == XBEE_OP_GET) ? xbee_at_table_validate_get(command)
                               : xbee_at_table_validate_set(command, value, len);
  if (status != SL_STATUS_OK) {
    return status;
  }

  if ((op != XBEE_OP_GET) && (detected_mode != XBEE_MODE_TRANSPARENT)) {
    const xbee_at_entry_t *entry = xbee_at_table_find(command);

    // Some commands only work from Command mode, whatever AP says
    // (manual lines 5810 to 5814).
    if ((entry != NULL)
        && ((entry->flags & XBEE_AT_FLAG_CMD_MODE_ONLY) != 0U)) {
      return SL_STATUS_NOT_SUPPORTED;
    }
  }

  user_req = request;
  status = start_request(request, command, op, value, len,
                         XBEE_API_LOCAL_AT_TIMEOUT_MS);
  if (status != SL_STATUS_OK) {
    user_req = NULL;
  }

  return status;
}

/***************************************************************************//**
 * Initialise the drivers and start bring-up.
 ******************************************************************************/
sl_status_t xbee_init(const xbee_config_t *config)
{
  sl_status_t status;

  (void)memset(&info, 0, sizeof(info));
  (void)memset(&cfg, 0, sizeof(cfg));
  user_req = NULL;
  failure_result = SL_STATUS_OK;
  detected_mode = XBEE_MODE_AUTO;
  module_sm = 0U;
  module_d8 = PIN_FUNCTION_ENABLED;
  module_d9 = PIN_FUNCTION_ENABLED;

  if (config != NULL) {
    cfg = *config;
  } else {
    cfg.mode = XBEE_MODE_AUTO;
    cfg.power_cycle = true;
  }
  if (cfg.settle_ms == 0U) {
    cfg.settle_ms = XBEE_POWER_SETTLE_MS;
  }
  if (cfg.probe_timeout_ms == 0U) {
    cfg.probe_timeout_ms = XBEE_PROBE_TIMEOUT_MS;
  }

  status = xbee_power_init();
  if (status != SL_STATUS_OK) {
    return status;
  }
  status = xbee_reset_init();
  if (status != SL_STATUS_OK) {
    return status;
  }
  status = xbee_sleep_init();
  if (status != SL_STATUS_OK) {
    return status;
  }
  status = xbee_uart_init();
  if (status != SL_STATUS_OK) {
    return status;
  }

  // The parser starts unescaped: the probe frame is valid either way.
  status = xbee_api_init(false);
  if (status != SL_STATUS_OK) {
    return status;
  }
  (void)xbee_api_set_unsolicited_callback(on_api_frame, NULL);

  status = xbee_cmd_mode_init();
  if (status != SL_STATUS_OK) {
    return status;
  }
  (void)xbee_cmd_mode_set_data_callback(on_transparent_data, NULL);

  if (cfg.power_cycle) {
    status = xbee_power_off();
    if (status != SL_STATUS_OK) {
      return status;
    }
  }
  status = xbee_power_on();
  if (status != SL_STATUS_OK) {
    return status;
  }

  state = XBEE_STATE_SETTLING;
  state_deadline_tick = deadline_from_ms(cfg.settle_ms);

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Shut down.
 ******************************************************************************/
sl_status_t xbee_deinit(void)
{
  (void)xbee_api_abort_all(SL_STATUS_ABORT);
  (void)xbee_cmd_mode_abandon(SL_STATUS_ABORT);

  if (user_req != NULL) {
    finish_user_request(user_req, SL_STATUS_ABORT);
  }

  // Stop the receive operations before the line goes dead.
  (void)xbee_uart_deinit();
  (void)xbee_power_off();

  state = XBEE_STATE_OFF;
  detected_mode = XBEE_MODE_AUTO;
  info.valid = false;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Drive the facade and the active transport.
 ******************************************************************************/
sl_status_t xbee_process(void)
{
  if (state == XBEE_STATE_OFF) {
    return SL_STATUS_OK;
  }

  // Both transports are driven: during detection the mode is not settled yet,
  // and each ignores traffic that is not its own.
  if (mode_is_api(detected_mode) || (detected_mode == XBEE_MODE_AUTO)) {
    (void)xbee_api_process();
  }
  if (!mode_is_api(detected_mode)) {
    (void)xbee_cmd_mode_process();
  }

  if (state == XBEE_STATE_READY) {
    if (user_req != NULL) {
      xbee_at_req_t *request = user_req;

      poll_request(request);

      // A mode change only takes effect locally once the module has accepted
      // it; switching sooner would leave the transports talking past it.
      if ((pending_mode != XBEE_MODE_AUTO)
          && (pending_mode_req == request)
          && xbee_at_req_complete(request)) {
        if (request->result == SL_STATUS_OK) {
          (void)adopt_mode(pending_mode);
          if (pending_mode == XBEE_MODE_TRANSPARENT) {
            // The module leaves API mode at once, so any parser state is stale.
            (void)xbee_uart_rx_flush();
          }
        }
        pending_mode = XBEE_MODE_AUTO;
        pending_mode_req = NULL;
      }
    }
    return SL_STATUS_OK;
  }

  if (state == XBEE_STATE_ASLEEP) {
    process_sleep();
    return SL_STATUS_OK;
  }
  if (state == XBEE_STATE_FAILED) {
    return SL_STATUS_OK;
  }

  process_bringup();

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Current bring-up state.
 ******************************************************************************/
xbee_state_t xbee_get_state(void)
{
  return state;
}

/***************************************************************************//**
 * Report whether the module is usable.
 ******************************************************************************/
bool xbee_is_ready(void)
{
  return (state == XBEE_STATE_READY);
}

/***************************************************************************//**
 * Why bring-up failed.
 ******************************************************************************/
sl_status_t xbee_get_result(void)
{
  return failure_result;
}

/***************************************************************************//**
 * The mode the module was found to be in.
 ******************************************************************************/
xbee_mode_t xbee_get_mode(void)
{
  return detected_mode;
}

/***************************************************************************//**
 * Parameters read during bring-up.
 ******************************************************************************/
sl_status_t xbee_get_info(xbee_info_t *out)
{
  if (out == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (!info.valid) {
    return SL_STATUS_NOT_READY;
  }

  *out = info;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Read a parameter.
 ******************************************************************************/
sl_status_t xbee_at_get(uint16_t command, xbee_at_req_t *request)
{
  return submit(command, XBEE_OP_GET, NULL, 0U, request);
}

/***************************************************************************//**
 * Write a parameter, applied immediately.
 ******************************************************************************/
sl_status_t xbee_at_set(uint16_t command,
                        const uint8_t *value,
                        uint16_t len,
                        xbee_at_req_t *request)
{
  return submit(command, XBEE_OP_SET, value, len, request);
}

/***************************************************************************//**
 * Write a parameter to be applied later.
 ******************************************************************************/
sl_status_t xbee_at_queue_set(uint16_t command,
                              const uint8_t *value,
                              uint16_t len,
                              xbee_at_req_t *request)
{
  return submit(command, XBEE_OP_QUEUE_SET, value, len, request);
}

/***************************************************************************//**
 * Write a numeric parameter from an unsigned value.
 ******************************************************************************/
sl_status_t xbee_at_set_u32(uint16_t command,
                            uint32_t value,
                            xbee_at_req_t *request)
{
  const xbee_at_entry_t *entry = xbee_at_table_find(command);
  uint8_t buf[4];
  uint16_t width;

  if (entry == NULL) {
    return SL_STATUS_NOT_FOUND;
  }

  width = (entry->max_len > 4U) ? 4U : entry->max_len;
  if (width == 0U) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // The module reads a parameter as a big-endian integer of the width it
  // expects, so the value is written right aligned in that width.
  byte_util_write_be32(buf, value);

  return submit(command, XBEE_OP_SET, &buf[4U - width], width, request);
}

/***************************************************************************//**
 * Run a command that takes no parameter.
 ******************************************************************************/
sl_status_t xbee_at_exec(uint16_t command, xbee_at_req_t *request)
{
  return submit(command, XBEE_OP_EXEC, NULL, 0U, request);
}

/***************************************************************************//**
 * Report whether a request has finished.
 ******************************************************************************/
bool xbee_at_req_complete(const xbee_at_req_t *request)
{
  if (request == NULL) {
    return false;
  }

  return ((request->state == XBEE_REQ_DONE)
          || (request->state == XBEE_REQ_FAILED));
}

/***************************************************************************//**
 * Read a completed request's value as a 64-bit unsigned integer.
 ******************************************************************************/
sl_status_t xbee_at_req_value_u64(const xbee_at_req_t *request, uint64_t *value)
{
  if ((request == NULL) || (value == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }
  if ((request->state != XBEE_REQ_DONE) || (request->value_len == 0U)) {
    return SL_STATUS_INVALID_STATE;
  }

  return byte_util_be_to_u64(request->value, request->value_len, value);
}

/***************************************************************************//**
 * Read a completed request's value as a 32-bit unsigned integer.
 ******************************************************************************/
sl_status_t xbee_at_req_value_u32(const xbee_at_req_t *request, uint32_t *value)
{
  uint64_t wide = 0U;
  sl_status_t status;

  if (value == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  status = xbee_at_req_value_u64(request, &wide);
  if (status != SL_STATUS_OK) {
    return status;
  }
  if (wide > (uint64_t)UINT32_MAX) {
    return SL_STATUS_INVALID_RANGE;
  }

  *value = (uint32_t)wide;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Read a completed request's value as a NUL-terminated string.
 ******************************************************************************/
sl_status_t xbee_at_req_value_str(const xbee_at_req_t *request,
                                  char *out,
                                  uint16_t cap)
{
  if ((request == NULL) || (out == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }
  if (request->state != XBEE_REQ_DONE) {
    return SL_STATUS_INVALID_STATE;
  }
  if (((uint32_t)request->value_len + 1U) > (uint32_t)cap) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  if (request->value_len > 0U) {
    (void)memcpy(out, request->value, request->value_len);
  }
  out[request->value_len] = '\0';

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Change the module's serial mode and follow it.
 ******************************************************************************/
sl_status_t xbee_set_mode(xbee_mode_t mode, xbee_at_req_t *request)
{
  uint8_t ap;
  sl_status_t status;

  switch (mode) {
    case XBEE_MODE_TRANSPARENT: ap = 0U; break;
    case XBEE_MODE_API1:        ap = 1U; break;
    case XBEE_MODE_API2:        ap = 2U; break;
    default:                    return SL_STATUS_INVALID_PARAMETER;
  }

  status = submit(XBEE_AT_AP, XBEE_OP_SET, &ap, 1U, request);
  if (status != SL_STATUS_OK) {
    return status;
  }

  // The local side follows once the module has acknowledged, which
  // xbee_process() notices; the target is remembered until then.
  info.ap = ap;
  pending_mode = mode;
  pending_mode_req = request;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Send a raw API frame.
 ******************************************************************************/
sl_status_t xbee_send_frame(xbee_frame_t *frame,
                            xbee_api_request_t *api_request,
                            uint32_t timeout_ms)
{
  if (state != XBEE_STATE_READY) {
    return SL_STATUS_NOT_READY;
  }
  if (!mode_is_api(detected_mode)) {
    return SL_STATUS_NOT_SUPPORTED;
  }

  return xbee_api_send(frame, api_request, timeout_ms);
}

/***************************************************************************//**
 * Send data over the air.
 ******************************************************************************/
sl_status_t xbee_send_data(uint64_t addr64,
                           uint8_t options,
                           const uint8_t *data,
                           uint16_t len,
                           xbee_api_request_t *api_request)
{
  xbee_frame_t frame;

  if (data == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (state != XBEE_STATE_READY) {
    return SL_STATUS_NOT_READY;
  }

  if (!mode_is_api(detected_mode)) {
    // Transparent mode sends to whatever DH and DL hold, so a destination given
    // here could not be honoured.
    if ((addr64 != XBEE_ADDR64_UNKNOWN) || (options != 0U)) {
      return SL_STATUS_NOT_SUPPORTED;
    }
    return xbee_cmd_mode_send_data(data, len);
  }

  xbee_frame_tx_request_defaults(&frame);
  frame.u.tx_request.dest_addr64 = addr64;
  frame.u.tx_request.options = options;
  frame.u.tx_request.data = data;
  frame.u.tx_request.data_len = len;

  return xbee_api_send(&frame, api_request, XBEE_API_TX_STATUS_TIMEOUT_MS);
}

/***************************************************************************//**
 * Register the callback for received data.
 ******************************************************************************/
sl_status_t xbee_set_data_callback(xbee_data_cb_t callback, void *user)
{
  data_cb = callback;
  data_user = user;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Register the callback for every unsolicited frame.
 ******************************************************************************/
sl_status_t xbee_set_frame_callback(xbee_frame_cb_t callback, void *user)
{
  frame_cb = callback;
  frame_user = user;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Register the callback for modem status changes.
 ******************************************************************************/
sl_status_t xbee_set_modem_status_callback(xbee_modem_status_cb_t callback,
                                           void *user)
{
  modem_cb = callback;
  modem_user = user;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Reset the module and run bring-up again.
 ******************************************************************************/
sl_status_t xbee_hw_reset(void)
{
  sl_status_t status;

  // Nothing in flight can survive the reset.
  (void)xbee_api_abort_all(SL_STATUS_ABORT);
  (void)xbee_cmd_mode_abandon(SL_STATUS_ABORT);
  if (user_req != NULL) {
    finish_user_request(user_req, SL_STATUS_ABORT);
  }
  (void)xbee_uart_rx_flush();

  status = xbee_reset_pulse_start();
  if (status != SL_STATUS_OK) {
    return status;
  }

  info.valid = false;
  failure_result = SL_STATUS_OK;
  detected_mode = XBEE_MODE_AUTO;
  // The parser goes back to unescaped, because the probe frame that follows is
  // valid in either API mode.
  (void)xbee_api_set_escaped(false);
  state = XBEE_STATE_RESETTING;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Ask the module to sleep.
 ******************************************************************************/
sl_status_t xbee_sleep_enter(void)
{
  sl_status_t status;

  if (state != XBEE_STATE_READY) {
    return SL_STATUS_NOT_READY;
  }
  // The module will not sleep while it is processing a command or holds queued
  // serial data (manual lines 4789 to 4811).
  if ((user_req != NULL)
      || (xbee_cmd_mode_get_state() != XBEE_CMD_STATE_IDLE)) {
    return SL_STATUS_INVALID_STATE;
  }
  // The request line only does anything in a pin-based sleep mode, with both
  // pins routed.
  if (((module_sm != SM_PIN_SLEEP) && (module_sm != SM_CYCLIC_PIN_WAKE))
      || (module_d8 != PIN_FUNCTION_ENABLED)
      || (module_d9 != PIN_FUNCTION_ENABLED)) {
    return SL_STATUS_NOT_SUPPORTED;
  }

  status = xbee_sleep_request(true);
  if (status != SL_STATUS_OK) {
    return status;
  }

  sleep_confirmed = false;
  sleep_requested = true;
  state = XBEE_STATE_ASLEEP;
  state_deadline_tick = deadline_from_ms(XBEE_SLEEP_ENTER_TIMEOUT_MS);

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Wake the module.
 ******************************************************************************/
sl_status_t xbee_sleep_exit(void)
{
  sl_status_t status;

  if (state != XBEE_STATE_ASLEEP) {
    return SL_STATUS_INVALID_STATE;
  }

  status = xbee_sleep_request(false);
  if (status != SL_STATUS_OK) {
    return status;
  }

  // The state stays ASLEEP until the status line confirms the module is awake,
  // so a request cannot be sent to a module that has not come back yet.
  sleep_requested = false;
  state_deadline_tick = deadline_from_ms(XBEE_SLEEP_EXIT_TIMEOUT_MS);

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Read the module's sleep status line.
 ******************************************************************************/
sl_status_t xbee_is_awake(bool *awake)
{
  return xbee_sleep_is_awake(awake);
}
