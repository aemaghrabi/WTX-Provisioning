/***************************************************************************//**
 * @file
 * @brief Transparent UART bridge between the XBee module and the terminal.
 *
 * While the bridge is active the MCU is not a participant on the link. Every
 * byte the terminal sends is handed to the module unchanged and every byte the
 * module sends is handed to the terminal unchanged: nothing is framed, parsed,
 * translated or generated. With both EUSARTs at the same baud rate the result
 * is equivalent to wiring the terminal straight to the module, which is what a
 * vendor tool such as XCTU needs in order to do the things the AT facade cannot
 * express: API frame exchanges, firmware updates and its own terminal.
 *
 * There are two ways in.
 *
 * The dedicated build, XBEE_APP=XBEE_APP_BRIDGE, calls xbee_bridge_init() and
 * then xbee_bridge_process() forever. It applies the module supply itself, waits
 * for it to boot and forwards from then on. Nothing else is in the image and
 * there is no way out but a reset.
 *
 * The console build calls xbee_bridge_enter() from its "bridge" command and
 * drives xbee_bridge_process() until xbee_bridge_is_active() goes false, which
 * happens when the operator sends the escape sequence. The facade must not be
 * driven while the bridge holds the transport: both would drain the same
 * receive ring.
 *
 * @note The one byte value that is not forwarded immediately is
 *       XBEE_BRIDGE_ESCAPE_CHAR following an idle line, which is held for up to
 *       one guard time while the bridge waits to see whether the sequence
 *       completes. It is forwarded if it does not. The module's own "+++" is
 *       never touched.
 ******************************************************************************/

#ifndef XBEE_BRIDGE_H
#define XBEE_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

#include "xbee_bridge_config.h"

/// How a bridge session is started.
typedef struct {
  /// Watch the terminal for the escape sequence and leave when it arrives.
  /// False in the dedicated build, which has nowhere to return to.
  bool allow_escape;

  /// Drive xbee_process() until any Command mode session has closed before
  /// forwarding starts. True when entering from the console, where the facade
  /// is running; false in the dedicated build, where it was never initialised.
  bool quiesce_facade;

  /// Apply the module supply and wait XBEE_BRIDGE_SETTLE_MS for it to boot,
  /// and arm the receive path. True in the dedicated build; false when entering
  /// from the console, where the module is already up and re-arming would abort
  /// the facade's queued receive operations.
  bool own_transport;
} xbee_bridge_params_t;

/// What a session carried, for the report on the way out.
typedef struct {
  uint32_t to_module_bytes;    ///< Forwarded from the terminal to the module.
  uint32_t to_terminal_bytes;  ///< Forwarded from the module to the terminal.
  uint32_t dropped_bytes;      ///< Terminal bytes lost because the ring was full.
  uint32_t rx_overruns;        ///< Module bytes lost in the driver's ring.
} xbee_bridge_stats_t;

/***************************************************************************//**
 * Start the dedicated bridge application.
 *
 * Applies the module supply and arms the receive path. Forwarding begins once
 * the module has had XBEE_BRIDGE_SETTLE_MS to boot; drive
 * xbee_bridge_process() until then and from then on.
 *
 * @return SL_STATUS_OK on success, or the first error from the supply line or
 *         the transport.
 ******************************************************************************/
sl_status_t xbee_bridge_init(void);

/***************************************************************************//**
 * Advance the bridge. Call once per pass of the super loop.
 *
 * Returns quickly. The only place it can wait is inside a bounded write to the
 * terminal, whose transmit path in the SDK is a polled loop; the chunk size is
 * chosen so that wait stays well inside the time the terminal's receive buffer
 * can absorb.
 ******************************************************************************/
void xbee_bridge_process(void);

/***************************************************************************//**
 * Take the link over.
 *
 * @param[in] params How to run the session. NULL is rejected rather than
 *                   defaulted: whether the bridge owns the transport is not
 *                   something to get wrong by omission.
 *
 * @return SL_STATUS_OK once the session has started,
 *         SL_STATUS_NULL_POINTER if params is NULL,
 *         SL_STATUS_INVALID_STATE if a session is already running,
 *         or the error that prevented the transport from starting.
 ******************************************************************************/
sl_status_t xbee_bridge_enter(const xbee_bridge_params_t *params);

/***************************************************************************//**
 * Give the link back.
 *
 * Restores the log level and the terminal's line ending translation to what
 * they were when the session started. Safe to call when no session is running.
 *
 * @return SL_STATUS_OK on success, or the error that prevented the terminal
 *         from being restored.
 ******************************************************************************/
sl_status_t xbee_bridge_leave(void);

/***************************************************************************//**
 * Report whether bytes are still being forwarded.
 *
 * Goes false on its own when the escape sequence completes, which is how the
 * console learns that the operator wants the prompt back.
 *
 * @return true while a session is running, including while it is settling or
 *         waiting for a Command mode session to close.
 ******************************************************************************/
bool xbee_bridge_is_active(void);

/***************************************************************************//**
 * Read what the session has carried so far.
 *
 * The counters survive xbee_bridge_leave(), so they can be reported after the
 * session has ended. They are cleared by the next xbee_bridge_enter().
 *
 * @param[out] stats Destination.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if stats is NULL.
 ******************************************************************************/
sl_status_t xbee_bridge_get_stats(xbee_bridge_stats_t *stats);

#endif  // XBEE_BRIDGE_H
