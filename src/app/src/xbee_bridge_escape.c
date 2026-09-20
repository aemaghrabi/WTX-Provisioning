/***************************************************************************//**
 * @file
 * @brief Escape sequence detector for the transparent UART bridge.
 ******************************************************************************/

#include <stddef.h>

#include "xbee_bridge_escape.h"

/***************************************************************************//**
 * Report whether a guard window has elapsed, tolerating counter wrap.
 *
 * The millisecond counter the bridge derives from the sleeptimer wraps, so the
 * comparison is on the signed difference rather than on the values.
 ******************************************************************************/
static bool guard_elapsed(uint32_t now_ms, uint32_t since_ms)
{
  return ((int32_t)(now_ms - since_ms) >= (int32_t)XBEE_BRIDGE_ESCAPE_GUARD_MS);
}

/***************************************************************************//**
 * Empty the output record.
 ******************************************************************************/
static void out_clear(xbee_bridge_escape_out_t *out)
{
  out->release_len = 0U;
  out->exit_bridge = false;
}

/***************************************************************************//**
 * Append one byte to the bytes the caller is to forward.
 ******************************************************************************/
static void out_push(xbee_bridge_escape_out_t *out, uint8_t byte)
{
  if (out->release_len < (uint8_t)(sizeof(out->release))) {
    out->release[out->release_len] = byte;
    out->release_len++;
  }
}

/***************************************************************************//**
 * Release every escape character held back so far, in arrival order.
 ******************************************************************************/
static void release_held(xbee_bridge_escape_t *esc,
                         xbee_bridge_escape_out_t *out)
{
  uint8_t i;

  for (i = 0U; i < esc->held; i++) {
    out_push(out, (uint8_t)XBEE_BRIDGE_ESCAPE_CHAR);
  }

  esc->held = 0U;
  esc->armed = false;
}

void xbee_bridge_escape_reset(xbee_bridge_escape_t *esc, uint32_t now_ms)
{
  if (esc == NULL) {
    return;
  }

  esc->last_byte_ms = now_ms;
  esc->held = 0U;
  esc->armed = false;
}

void xbee_bridge_escape_feed(xbee_bridge_escape_t *esc,
                             uint8_t byte,
                             uint32_t now_ms,
                             xbee_bridge_escape_out_t *out)
{
  if (out == NULL) {
    return;
  }

  out_clear(out);

  if (esc == NULL) {
    return;
  }

  if (esc->armed) {
    // The full sequence was seen, but a byte arrived before the trailing guard
    // closed. That makes it payload after all, so all of it goes on its way.
    release_held(esc, out);
    out_push(out, byte);
    esc->last_byte_ms = now_ms;
    return;
  }

  if (byte == (uint8_t)XBEE_BRIDGE_ESCAPE_CHAR) {
    if (esc->held > 0U) {
      esc->held++;
      esc->last_byte_ms = now_ms;
      if (esc->held >= (uint8_t)XBEE_BRIDGE_ESCAPE_COUNT) {
        esc->armed = true;
      }
      return;
    }

    if (guard_elapsed(now_ms, esc->last_byte_ms)) {
      // Silence, then the escape character: a candidate. Held back, because
      // forwarding it now and exiting later would send the module a byte the
      // operator never meant it to see.
      esc->held = 1U;
      esc->last_byte_ms = now_ms;
      return;
    }

    // In the middle of a stream, so it is data like any other.
    out_push(out, byte);
    esc->last_byte_ms = now_ms;
    return;
  }

  // Anything else ends a candidate. Order matters: the held characters were
  // typed first.
  release_held(esc, out);
  out_push(out, byte);
  esc->last_byte_ms = now_ms;
}

void xbee_bridge_escape_idle(xbee_bridge_escape_t *esc,
                             uint32_t now_ms,
                             xbee_bridge_escape_out_t *out)
{
  if (out == NULL) {
    return;
  }

  out_clear(out);

  if ((esc == NULL) || (esc->held == 0U)) {
    return;
  }

  if (!guard_elapsed(now_ms, esc->last_byte_ms)) {
    return;
  }

  if (esc->armed) {
    // Silence before and after the full count. Nothing is forwarded: those
    // characters were addressed to the bridge, not to the module.
    esc->held = 0U;
    esc->armed = false;
    out->exit_bridge = true;
    return;
  }

  // A shorter run that never grew into the sequence. It was only ever held to
  // see whether it would, so it goes on to the module now.
  release_held(esc, out);
}
