/***************************************************************************//**
 * @file
 * @brief Transparent UART bridge between the XBee module and the terminal.
 ******************************************************************************/

#include <stddef.h>

#include "sl_sleeptimer.h"
#include "sl_iostream_eusart_VCOM_config.h"
#include "sl_uartdrv_eusart_XBEE_config.h"

#include "app_log.h"
#include "console_uart.h"
#include "ring_buffer.h"
#include "xbee_power.h"
#include "xbee_reset.h"
#include "xbee_sleep.h"
#include "xbee_uart.h"

#include "xbee.h"

#include "xbee_bridge.h"
#include "xbee_bridge_config.h"
#include "xbee_bridge_escape.h"

// ---------------------------------------------------------------------------
// The two links must run at the same rate. A bridge between different rates
// loses data in the faster direction as soon as a transfer lasts longer than
// the slower side's buffer, and neither link has flow control to make that
// visible (docs/history/2026-09-18-eusart-flow-control-off.md), so the loss
// would show up as corrupted frames rather than as an error. Refuse it here
// instead of debugging it on hardware.
//
// Both values are owned by Simplicity Studio. Change the VCOM instance in
// Software Components -> IO Stream: EUSART -> VCOM, or the XBEE instance in
// uartdrv_eusart -> XBEE, and regenerate.

#if SL_IOSTREAM_EUSART_VCOM_BAUDRATE != SL_UARTDRV_EUSART_XBEE_BAUDRATE
#error "The VCOM and XBEE instances run at different baud rates, so a transparent \
bridge between them would lose data. Set both to the same rate in Simplicity Studio."
#endif

/// Where the bridge is.
typedef enum {
  BRIDGE_STATE_OFF,      ///< No session.
  BRIDGE_STATE_SETTLE,   ///< Supply applied, waiting for the module to boot.
  BRIDGE_STATE_QUIESCE,  ///< Waiting for a Command mode session to close.
  BRIDGE_STATE_ACTIVE,   ///< Forwarding.
} bridge_state_t;

/// Storage for the ring carrying terminal bytes towards the module.
static uint8_t tx_ring_storage[XBEE_BRIDGE_TX_RING_SIZE];

/// That ring. Single producer and single consumer, both in the super loop.
static ring_buffer_t tx_ring;

/// Where the bridge is.
static bridge_state_t state = BRIDGE_STATE_OFF;

/// How this session was asked to run.
static xbee_bridge_params_t params;

/// Escape sequence detector, used only when params.allow_escape.
static xbee_bridge_escape_t escape;

/// Tick the session started at, and the origin of its millisecond clock.
static uint32_t session_start_tick;

/// Tick by which the current settling or quiescing step must be over.
static uint32_t step_deadline;

/// True once the exit command has been sent while quiescing, so it is not sent
/// again on every pass.
static bool quiesce_exit_sent;

/// Log level in force when the session started, restored when it ends.
static app_log_level_t saved_log_level;

/// Counters for this session.
static xbee_bridge_stats_t stats;

/// Driver receive overrun count when the session started. The driver's counter
/// is cumulative, so the session's own figure is the difference.
static uint32_t overruns_at_start;

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

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
 * Milliseconds since the session started.
 *
 * The whole elapsed tick count is converted on every call rather than a delta
 * accumulated: a super-loop pass is far shorter than one tick, so converting
 * deltas would truncate every one of them to zero and the clock would never
 * move. The subtraction is modular, so the underlying counter's wrap is
 * handled; the millisecond value itself wraps after about 36 hours of
 * continuous bridging, which the guard comparison sees as "not elapsed" and
 * which costs at worst one retry of the escape sequence.
 ******************************************************************************/
static uint32_t clock_now_ms(void)
{
  uint32_t elapsed = sl_sleeptimer_get_tick_count() - session_start_tick;

  return sl_sleeptimer_tick_to_ms(elapsed);
}

// ---------------------------------------------------------------------------
// Forwarding
// ---------------------------------------------------------------------------

/***************************************************************************//**
 * Queue one byte for the module.
 *
 * A full ring means the byte is gone: there is no flow control to hold the
 * terminal off with, and holding it in the VCOM receive buffer instead would
 * only move the loss somewhere it is not counted.
 ******************************************************************************/
static void queue_for_module(uint8_t byte)
{
  if (ring_buffer_push(&tx_ring, byte) == SL_STATUS_OK) {
    stats.to_module_bytes++;
  } else {
    stats.dropped_bytes++;
  }
}

/***************************************************************************//**
 * Collect whatever the terminal has sent and pass it on.
 ******************************************************************************/
static void service_terminal(void)
{
  uint8_t buf[XBEE_BRIDGE_VCOM_CHUNK];
  uint16_t count = 0U;
  uint16_t i;

  if (console_uart_read((char *)buf, (uint16_t)sizeof(buf), &count)
      != SL_STATUS_OK) {
    return;
  }

  for (i = 0U; i < count; i++) {
    if (params.allow_escape) {
      xbee_bridge_escape_out_t out;
      uint8_t j;

      xbee_bridge_escape_feed(&escape, buf[i], clock_now_ms(), &out);

      for (j = 0U; j < out.release_len; j++) {
        queue_for_module(out.release[j]);
      }
    } else {
      queue_for_module(buf[i]);
    }
  }
}

/***************************************************************************//**
 * Hand the next queued bytes to the module's transport.
 *
 * One transfer is in flight at a time, so this moves at most one chunk per
 * pass and comes back for the rest once the driver reports the line free.
 ******************************************************************************/
static void service_module_tx(void)
{
  uint8_t buf[XBEE_BRIDGE_XBEE_CHUNK];
  uint16_t count = 0U;

  if (xbee_uart_tx_busy()) {
    return;
  }

  if (ring_buffer_read(&tx_ring, buf, (uint16_t)sizeof(buf), &count)
      != SL_STATUS_OK) {
    return;
  }
  if (count == 0U) {
    return;
  }

  if (xbee_uart_write(buf, count) != SL_STATUS_OK) {
    // The transport was claimed between the check and the write, or refused
    // the operation. The bytes have already left the ring, so they are lost;
    // count them the same way a ring overflow is counted.
    stats.dropped_bytes += count;
    stats.to_module_bytes -= count;
  }
}

/***************************************************************************//**
 * Pass whatever the module has sent on to the terminal.
 ******************************************************************************/
static void service_module_rx(void)
{
  uint8_t buf[XBEE_BRIDGE_VCOM_CHUNK];
  uint16_t count = 0U;

  if (xbee_uart_read(buf, (uint16_t)sizeof(buf), &count) != SL_STATUS_OK) {
    return;
  }
  if (count == 0U) {
    return;
  }

  if (console_uart_write_raw(buf, count) == SL_STATUS_OK) {
    stats.to_terminal_bytes += count;
  }
}

/***************************************************************************//**
 * Close a guard window: leave the bridge, or release a candidate that was not
 * the escape sequence after all.
 ******************************************************************************/
static void service_escape(void)
{
  xbee_bridge_escape_out_t out;
  uint8_t i;

  if (!params.allow_escape) {
    return;
  }

  xbee_bridge_escape_idle(&escape, clock_now_ms(), &out);

  for (i = 0U; i < out.release_len; i++) {
    queue_for_module(out.release[i]);
  }

  if (out.exit_bridge) {
    state = BRIDGE_STATE_OFF;
  }
}

// ---------------------------------------------------------------------------
// Session lifetime
// ---------------------------------------------------------------------------

/***************************************************************************//**
 * Switch the terminal to a binary stream and start forwarding.
 *
 * Line ending translation has to go: the terminal instance inserts a carriage
 * return before every line feed it sends
 * (SL_IOSTREAM_EUSART_VCOM_CONVERT_BY_DEFAULT_LF_TO_CRLF), which is right for a
 * console and wrong for a frame that happens to contain 0x0A.
 *
 * The log was silenced when the session was entered rather than here, because
 * the steps before this one can log too.
 ******************************************************************************/
static void begin_active(void)
{
  xbee_uart_stats_t uart_stats;

  (void)console_uart_set_binary(true);

  (void)ring_buffer_clear(&tx_ring);
  (void)xbee_uart_rx_flush();

  if (xbee_uart_get_stats(&uart_stats) == SL_STATUS_OK) {
    overruns_at_start = uart_stats.rx_overruns;
  } else {
    overruns_at_start = 0U;
  }

  xbee_bridge_escape_reset(&escape, clock_now_ms());

  state = BRIDGE_STATE_ACTIVE;
}

/***************************************************************************//**
 * Wait for any Command mode session to close.
 *
 * The Command mode transport keeps a session open for CT after each command, so
 * a console that has just read a parameter leaves the module sitting in Command
 * mode. A tool connecting through the bridge would find its first data answered
 * with ERROR, so the session is closed first.
 ******************************************************************************/
static void advance_quiesce(void)
{
  xbee_cmd_mode_state_t cmd_state;

  (void)xbee_process();

  cmd_state = xbee_cmd_mode_get_state();

  if (cmd_state == XBEE_CMD_STATE_IDLE) {
    begin_active();
    return;
  }

  if ((cmd_state == XBEE_CMD_STATE_ACTIVE) && !quiesce_exit_sent) {
    if (xbee_cmd_mode_exit(NULL) == SL_STATUS_OK) {
      quiesce_exit_sent = true;
    }
  }

  if (tick_reached(step_deadline)) {
    // The module never acknowledged the exit. Forward anyway: a bridge that
    // refuses to open is worse than one that opens with the module in a state
    // the tool can recover from itself, and Command mode expires on its own.
    (void)xbee_cmd_mode_abandon(SL_STATUS_TIMEOUT);
    begin_active();
  }
}

sl_status_t xbee_bridge_enter(const xbee_bridge_params_t *par)
{
  sl_status_t status;

  if (par == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (state != BRIDGE_STATE_OFF) {
    return SL_STATUS_INVALID_STATE;
  }

  params = *par;

  // Bind the terminal here rather than relying on a caller having done it. The
  // console build has, from cli_init(); the dedicated build has no console and
  // would otherwise reach the forwarding loop with no stream, where every read
  // and every write fails quietly and the bridge looks dead.
  status = console_uart_init();
  if (status != SL_STATUS_OK) {
    return status;
  }

  status = ring_buffer_init(&tx_ring, tx_ring_storage,
                            (uint16_t)XBEE_BRIDGE_TX_RING_SIZE);
  if (status != SL_STATUS_OK) {
    return status;
  }

  stats.to_module_bytes = 0U;
  stats.to_terminal_bytes = 0U;
  stats.dropped_bytes = 0U;
  stats.rx_overruns = 0U;
  overruns_at_start = 0U;
  quiesce_exit_sent = false;
  session_start_tick = sl_sleeptimer_get_tick_count();

  // Everything that can still fail happens before the log is silenced, so a
  // refused session leaves the caller with a log to read.
  if (params.own_transport) {
    // Every control line the module has, in the order xbee_init() uses. All of
    // them matter, not just the supply: a line this build leaves alone is left
    // in the MCU's own reset state, which is disabled and floating, and the
    // module reads it through its internal pull-up. A floating XBEE_SLEEPRQ
    // therefore reads as a sleep request, and a module asleep answers nothing
    // at all, which is indistinguishable from a bridge that does not work.
    status = xbee_power_init();
    if (status != SL_STATUS_OK) {
      return status;
    }

    // Open drain, released, so the module's pull-up holds it out of reset.
    status = xbee_reset_init();
    if (status != SL_STATUS_OK) {
      return status;
    }

    // Drives XBEE_SLEEPRQ low: awake, whatever SM the module is configured for.
    status = xbee_sleep_init();
    if (status != SL_STATUS_OK) {
      return status;
    }

    status = xbee_power_on();
    if (status != SL_STATUS_OK) {
      return status;
    }

    status = xbee_uart_init();
    if (status != SL_STATUS_OK) {
      (void)xbee_power_off();
      return status;
    }
  }

  // From here on the link belongs to whoever is on the other end, so the log
  // stops sharing it. Done before the settling and quiescing steps, not when
  // forwarding starts: driving the facade to close a Command mode session logs
  // too, and those lines would land in a stream the operator has already been
  // told is transparent.
  saved_log_level = app_log_get_level();
  (void)app_log_set_level(APP_LOG_LEVEL_NONE);

  if (params.own_transport) {
    step_deadline = deadline_from_ms(XBEE_BRIDGE_SETTLE_MS);
    state = BRIDGE_STATE_SETTLE;

    return SL_STATUS_OK;
  }

  if (params.quiesce_facade) {
    step_deadline = deadline_from_ms(XBEE_BRIDGE_QUIESCE_TIMEOUT_MS);
    state = BRIDGE_STATE_QUIESCE;

    return SL_STATUS_OK;
  }

  begin_active();

  return SL_STATUS_OK;
}

sl_status_t xbee_bridge_leave(void)
{
  sl_status_t status;

  if (state == BRIDGE_STATE_OFF) {
    // Already closed, which is the normal case: the escape sequence ends the
    // session and the caller then calls this to put the terminal back.
    status = SL_STATUS_OK;
  } else {
    state = BRIDGE_STATE_OFF;
    status = SL_STATUS_OK;
  }

  (void)console_uart_set_binary(false);
  (void)app_log_set_level(saved_log_level);

  return status;
}

bool xbee_bridge_is_active(void)
{
  return (state != BRIDGE_STATE_OFF);
}

sl_status_t xbee_bridge_get_stats(xbee_bridge_stats_t *out)
{
  if (out == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  *out = stats;

  return SL_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

sl_status_t xbee_bridge_init(void)
{
  static const xbee_bridge_params_t dedicated = {
    .allow_escape = false,    // Nothing to return to in this build.
    .quiesce_facade = false,  // The facade was never initialised.
    .own_transport = true,
  };

  return xbee_bridge_enter(&dedicated);
}

void xbee_bridge_process(void)
{
  switch (state) {
    case BRIDGE_STATE_SETTLE:
      if (tick_reached(step_deadline)) {
        begin_active();
      }
      break;

    case BRIDGE_STATE_QUIESCE:
      advance_quiesce();
      break;

    case BRIDGE_STATE_ACTIVE:
      // The terminal first: its receive buffer is the smallest in the system
      // and the only one that can overflow inside a single pass.
      service_terminal();
      service_module_tx();
      service_module_rx();
      service_escape();

      {
        xbee_uart_stats_t uart_stats;

        if (xbee_uart_get_stats(&uart_stats) == SL_STATUS_OK) {
          stats.rx_overruns = uart_stats.rx_overruns - overruns_at_start;
        }
      }
      break;

    case BRIDGE_STATE_OFF:
    default:
      break;
  }
}
