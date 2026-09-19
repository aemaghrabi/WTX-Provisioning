/***************************************************************************//**
 * @file
 * @brief Single-line input discipline for a character terminal.
 *
 * Assembles one command line from bytes arriving out of a serial port, one byte
 * per call, and reports what the caller should do about each one. Holds no
 * buffer of its own and knows nothing about the SDK: echo leaves through a
 * caller-supplied callback, so the same code runs on the target and under the
 * host tests.
 *
 * Usage:
 * @code
 * static char storage[128];
 * static line_edit_t le;
 *
 * line_edit_init(&le, storage, sizeof(storage), echo_cb, NULL);
 *
 * switch (line_edit_feed(&le, byte)) {
 *   case LINE_EDIT_READY:
 *     dispatch(line_edit_line(&le));
 *     line_edit_reset(&le);
 *     break;
 *   case LINE_EDIT_HELP:
 *     show_help(line_edit_line(&le));  // the line is kept, not consumed
 *     break;
 *   default:
 *     break;
 * }
 * @endcode
 *
 * What it handles:
 * - Printable ASCII, 0x20 to 0x7E.
 * - CR, LF and CRLF, which all end a line exactly once. See @ref line_edit_reset
 *   for why the CRLF state outlives a reset.
 * - Backspace (0x08) and DEL (0x7F), both erasing one character.
 * - Ctrl-C (0x03), abandoning the line.
 * - '?' as an immediate help request that does not disturb the line, which is
 *   what a Cisco-style console expects. Only in @ref LINE_EDIT_ECHO_ON, so that
 *   a password containing '?' still works.
 * - Escape sequences, swallowed rather than parsed, so that pressing an arrow
 *   key does not drop "[A" into the command line.
 *
 * What it does not do: command history, cursor movement within the line, and
 * tab completion. The cursor is always at the end of the line.
 ******************************************************************************/

#ifndef LINE_EDIT_H
#define LINE_EDIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sl_status.h"

/// What the caller should do about the byte just fed in.
typedef enum {
  LINE_EDIT_NONE,      ///< Byte consumed, nothing to do.
  LINE_EDIT_READY,     ///< The line is complete. Read it, then reset.
  LINE_EDIT_HELP,      ///< '?' typed. Show help for the line so far, do not reset.
  LINE_EDIT_ABORT,     ///< Ctrl-C. The line has already been discarded.
  LINE_EDIT_OVERFLOW,  ///< The line outgrew its buffer. See the note below.
} line_edit_event_t;

/// How typed characters are reflected back to the terminal.
typedef enum {
  LINE_EDIT_ECHO_ON,    ///< Echo each character. '?' means help.
  LINE_EDIT_ECHO_OFF,   ///< Echo nothing. Every character is literal.
  LINE_EDIT_ECHO_MASK,  ///< Echo one '*' per character. Every character is literal.
} line_edit_echo_mode_t;

/***************************************************************************//**
 * Emit text towards the terminal.
 *
 * Called from @ref line_edit_feed for echo. Must not call back into the line
 * editor.
 *
 * @param[in] text Characters to emit, not NUL-terminated.
 * @param[in] len  Number of characters.
 * @param[in] user Context given to @ref line_edit_init.
 ******************************************************************************/
typedef void (*line_edit_echo_fn_t)(const char *text, uint16_t len, void *user);

/// Line editor state. Treat the fields as private.
typedef struct {
  char                 *buf;        ///< Caller-provided storage, always NUL-terminated.
  uint16_t              cap;        ///< Size of buf in bytes, terminator included.
  uint16_t              len;        ///< Characters held, always below cap.
  line_edit_echo_mode_t echo;       ///< Echo policy.
  line_edit_echo_fn_t   echo_fn;    ///< Echo sink, may be NULL.
  void                 *user;       ///< Context for echo_fn.
  bool                  overflowed; ///< Line already reported as too long.
  bool                  pending_lf; ///< A CR was just seen; swallow one LF.
  uint8_t               esc;        ///< Escape sequence sub-state.
} line_edit_t;

/***************************************************************************//**
 * Initialise a line editor over caller-provided storage.
 *
 * Echo starts at @ref LINE_EDIT_ECHO_ON and the line starts empty.
 *
 * @param[out] le      Editor to initialise.
 * @param[in]  buf     Storage, valid for the lifetime of the editor. It holds
 *                     at most cap - 1 characters plus a terminator.
 * @param[in]  cap     Size of buf in bytes, at least 2.
 * @param[in]  echo_fn Echo sink, or NULL to echo nothing at all.
 * @param[in]  user    Context passed to echo_fn.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if le or buf is NULL,
 *         SL_STATUS_INVALID_PARAMETER if cap is below 2.
 ******************************************************************************/
sl_status_t line_edit_init(line_edit_t *le,
                           char *buf,
                           uint16_t cap,
                           line_edit_echo_fn_t echo_fn,
                           void *user);

/***************************************************************************//**
 * Choose how typed characters are echoed.
 *
 * Changing the mode does not disturb the line already held, so it is safe to
 * switch to @ref LINE_EDIT_ECHO_MASK for a password prompt and back afterwards.
 *
 * @param[in,out] le   Editor.
 * @param[in]     mode New echo policy.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if le is NULL,
 *         SL_STATUS_INVALID_PARAMETER if mode is out of range.
 ******************************************************************************/
sl_status_t line_edit_set_echo(line_edit_t *le, line_edit_echo_mode_t mode);

/***************************************************************************//**
 * Feed one received byte.
 *
 * On @ref LINE_EDIT_READY the line is available through @ref line_edit_line and
 * stays there until @ref line_edit_reset is called, so the caller can dispatch
 * from it without copying.
 *
 * On @ref LINE_EDIT_HELP the line is left exactly as it was, because the caller
 * has to inspect it to decide what help to print and the operator expects to
 * carry on typing afterwards.
 *
 * On @ref LINE_EDIT_OVERFLOW the line has already been discarded and every
 * further character is swallowed until the terminator, which then reports
 * @ref LINE_EDIT_NONE rather than @ref LINE_EDIT_READY. The caller therefore
 * reports the error once, on the overflow itself, and must not reset in
 * response: resetting would let the tail of the over-long line be taken as a
 * fresh command.
 *
 * @param[in,out] le Editor.
 * @param[in]     c  Received byte.
 *
 * @return The event, or @ref LINE_EDIT_NONE when le is NULL.
 ******************************************************************************/
line_edit_event_t line_edit_feed(line_edit_t *le, char c);

/***************************************************************************//**
 * The line held, always NUL-terminated.
 *
 * @param[in] le Editor.
 *
 * @return The line, or "" when le is NULL. Never NULL.
 ******************************************************************************/
const char *line_edit_line(const line_edit_t *le);

/***************************************************************************//**
 * Number of characters held, excluding the terminator.
 *
 * @param[in] le Editor.
 *
 * @return Character count, 0 when le is NULL.
 ******************************************************************************/
uint16_t line_edit_len(const line_edit_t *le);

/***************************************************************************//**
 * Discard the line and start a new one.
 *
 * The CRLF and escape sequence states deliberately survive this, because a
 * reset normally happens between the CR and the LF of one line ending: the
 * caller dispatches the command the CR completed and resets before the LF
 * arrives. Clearing the state here would let that LF end a second, empty line
 * and print a spurious prompt.
 *
 * @param[in,out] le Editor.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if le is NULL.
 ******************************************************************************/
sl_status_t line_edit_reset(line_edit_t *le);

#endif  // LINE_EDIT_H
