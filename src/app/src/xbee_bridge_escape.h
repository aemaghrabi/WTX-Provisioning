/***************************************************************************//**
 * @file
 * @brief Escape sequence detector for the transparent UART bridge.
 *
 * Private to the bridge. Split out because it is the one part of a transparent
 * bridge that is not transparent, and because it is pure: time is passed in
 * rather than read, so every window and every boundary is reachable from a host
 * test.
 *
 * The sequence is XBEE_BRIDGE_ESCAPE_COUNT repetitions of
 * XBEE_BRIDGE_ESCAPE_CHAR with at least XBEE_BRIDGE_ESCAPE_GUARD_MS of silence
 * before the first and after the last, which is the same discipline the module
 * applies to its own escape sequence.
 *
 * Nothing is discarded. A candidate that does not complete is released in the
 * order it arrived, so the only cost of the detector on a stream that never
 * means to escape is that an escape character following an idle line is
 * delayed by up to one guard time. Every other byte passes straight through.
 ******************************************************************************/

#ifndef XBEE_BRIDGE_ESCAPE_H
#define XBEE_BRIDGE_ESCAPE_H

#include <stdbool.h>
#include <stdint.h>

#include "xbee_bridge_config.h"

/// Detector state. Owned by the caller; treat the members as private.
typedef struct {
  uint32_t last_byte_ms;  ///< When the last byte arrived, held or forwarded.
  uint8_t  held;          ///< Escape characters held back, 0 to the full count.
  bool     armed;         ///< Full sequence seen, waiting for trailing silence.
} xbee_bridge_escape_t;

/// What the caller must do with the bytes it just offered.
typedef struct {
  /// Bytes to forward, in the order they arrived. Held characters come first.
  uint8_t release[XBEE_BRIDGE_ESCAPE_COUNT + 1U];
  uint8_t release_len;  ///< How many of @ref release are valid.
  bool    exit_bridge;  ///< The sequence completed. Leave the bridge.
} xbee_bridge_escape_out_t;

/***************************************************************************//**
 * Start a detection session.
 *
 * The current time is taken as the last byte seen, so the leading guard window
 * is measured from here: an escape character sent immediately is ordinary data.
 *
 * @param[out] esc    Detector to initialise.
 * @param[in]  now_ms Current time in milliseconds.
 ******************************************************************************/
void xbee_bridge_escape_reset(xbee_bridge_escape_t *esc, uint32_t now_ms);

/***************************************************************************//**
 * Offer one received byte.
 *
 * @param[in,out] esc    Detector.
 * @param[in]     byte   The byte the terminal sent.
 * @param[in]     now_ms Current time in milliseconds.
 * @param[out]    out    Bytes to forward, and whether to leave the bridge.
 *                       @ref xbee_bridge_escape_out_t::exit_bridge is never set
 *                       here: the sequence only completes on silence.
 ******************************************************************************/
void xbee_bridge_escape_feed(xbee_bridge_escape_t *esc,
                             uint8_t byte,
                             uint32_t now_ms,
                             xbee_bridge_escape_out_t *out);

/***************************************************************************//**
 * Let time pass. Call once per super-loop iteration, after any bytes.
 *
 * This is where a guard window closes: either the sequence completes and the
 * bridge is to be left, or a candidate that never grew into one is released.
 *
 * @param[in,out] esc    Detector.
 * @param[in]     now_ms Current time in milliseconds.
 * @param[out]    out    Bytes to forward, and whether to leave the bridge.
 ******************************************************************************/
void xbee_bridge_escape_idle(xbee_bridge_escape_t *esc,
                             uint32_t now_ms,
                             xbee_bridge_escape_out_t *out);

#endif  // XBEE_BRIDGE_ESCAPE_H
