/***************************************************************************//**
 * @file
 * @brief Single-line input discipline for a character terminal.
 ******************************************************************************/

#include "line_edit.h"

/// @name Control characters this module acts on
/// @{
#define LINE_EDIT_CH_ETX  '\x03'  ///< Ctrl-C.
#define LINE_EDIT_CH_BS   '\x08'  ///< Backspace.
#define LINE_EDIT_CH_LF   '\n'
#define LINE_EDIT_CH_CR   '\r'
#define LINE_EDIT_CH_ESC  '\x1B'
#define LINE_EDIT_CH_DEL  '\x7F'
/// @}

/// Smallest usable capacity: one character plus the terminator.
#define LINE_EDIT_MIN_CAP  2U

/// @name Escape sequence sub-states
///
/// Enough of a parser to swallow what an arrow key or a function key sends, and
/// no more. A full ANSI decoder is not wanted here: nothing in this console
/// acts on a cursor key, and the only job is to keep the bytes of a sequence
/// out of the command line.
/// @{
#define LINE_EDIT_ESC_NONE     0U  ///< Not in a sequence.
#define LINE_EDIT_ESC_GOT_ESC  1U  ///< Saw ESC, waiting to see what follows.
#define LINE_EDIT_ESC_GOT_CSI  2U  ///< In a control sequence, waiting for its final byte.
/// @}

/***************************************************************************//**
 * Send text to the terminal, if there is anywhere to send it.
 ******************************************************************************/
static void emit(const line_edit_t *le, const char *text, uint16_t len)
{
  if ((le->echo_fn != NULL) && (len > 0U)) {
    le->echo_fn(text, len, le->user);
  }
}

/***************************************************************************//**
 * Echo one typed character according to the echo policy.
 ******************************************************************************/
static void echo_char(const line_edit_t *le, char c)
{
  char masked = '*';

  switch (le->echo) {
    case LINE_EDIT_ECHO_ON:
      emit(le, &c, 1U);
      break;

    case LINE_EDIT_ECHO_MASK:
      emit(le, &masked, 1U);
      break;

    case LINE_EDIT_ECHO_OFF:
    default:
      break;
  }
}

/***************************************************************************//**
 * Erase the last echoed character from the terminal.
 *
 * Backspace alone only moves the cursor, so the character has to be overwritten
 * with a space and the cursor moved back again.
 ******************************************************************************/
static void echo_erase(const line_edit_t *le)
{
  static const char erase[] = { LINE_EDIT_CH_BS, ' ', LINE_EDIT_CH_BS };

  if (le->echo != LINE_EDIT_ECHO_OFF) {
    emit(le, erase, (uint16_t)sizeof(erase));
  }
}

/***************************************************************************//**
 * Move the terminal to the start of the next line.
 *
 * A bare line feed, not CR LF: the transport owns the line ending. On this
 * project that is the VCOM iostream, which is configured to expand LF to CRLF
 * (SL_IOSTREAM_EUSART_VCOM_CONVERT_BY_DEFAULT_LF_TO_CRLF), and emitting the CR
 * here as well would send it twice. app_log writes its lines the same way.
 ******************************************************************************/
static void echo_newline(const line_edit_t *le)
{
  static const char newline[] = { LINE_EDIT_CH_LF };

  emit(le, newline, (uint16_t)sizeof(newline));
}

/***************************************************************************//**
 * Empty the line, leaving the CRLF and escape states alone.
 ******************************************************************************/
static void clear_line(line_edit_t *le)
{
  le->len = 0U;
  le->buf[0] = '\0';
  le->overflowed = false;
}

/***************************************************************************//**
 * Handle a byte that arrived in the middle of an escape sequence.
 *
 * @return true when the byte belonged to the sequence and was swallowed.
 ******************************************************************************/
static bool consume_escape(line_edit_t *le, char c)
{
  uint8_t byte = (uint8_t)c;

  switch (le->esc) {
    case LINE_EDIT_ESC_GOT_ESC:
      // CSI ("ESC [") and SS3 ("ESC O") introduce the sequences a keyboard
      // sends. Anything else after ESC is a two-byte sequence, already complete.
      le->esc = ((c == '[') || (c == 'O'))
                ? LINE_EDIT_ESC_GOT_CSI : LINE_EDIT_ESC_NONE;
      return true;

    case LINE_EDIT_ESC_GOT_CSI:
      // Parameter bytes are 0x30 to 0x3F and intermediates 0x20 to 0x2F; the
      // sequence ends at the first final byte, 0x40 to 0x7E. Keeping to the
      // real rule costs one comparison and handles the four-byte keys, such as
      // the Delete key's "ESC [ 3 ~", that a fixed-length swallow would break.
      if ((byte >= 0x40U) && (byte <= 0x7EU)) {
        le->esc = LINE_EDIT_ESC_NONE;
      }
      return true;

    case LINE_EDIT_ESC_NONE:
    default:
      return false;
  }
}

/***************************************************************************//**
 * End the current line.
 ******************************************************************************/
static line_edit_event_t finish_line(line_edit_t *le)
{
  echo_newline(le);

  if (le->overflowed) {
    // The caller was already told on the overflow itself. Reporting the line
    // now would hand it the truncated remains of a command it has rejected.
    clear_line(le);
    return LINE_EDIT_NONE;
  }

  return LINE_EDIT_READY;
}

/***************************************************************************//**
 * Append one printable character.
 ******************************************************************************/
static line_edit_event_t append_char(line_edit_t *le, char c)
{
  if (le->overflowed) {
    // Already reported. Swallow the rest of the line quietly.
    return LINE_EDIT_NONE;
  }

  if ((uint16_t)(le->len + 1U) >= le->cap) {
    le->overflowed = true;
    le->len = 0U;
    le->buf[0] = '\0';
    return LINE_EDIT_OVERFLOW;
  }

  le->buf[le->len] = c;
  le->len++;
  le->buf[le->len] = '\0';
  echo_char(le, c);

  return LINE_EDIT_NONE;
}

/***************************************************************************//**
 * Erase the last character of the line.
 ******************************************************************************/
static line_edit_event_t erase_char(line_edit_t *le)
{
  if ((le->len == 0U) || le->overflowed) {
    // Nothing to erase. Staying silent rather than echoing is what keeps the
    // cursor from walking back over the prompt itself.
    return LINE_EDIT_NONE;
  }

  le->len--;
  le->buf[le->len] = '\0';
  echo_erase(le);

  return LINE_EDIT_NONE;
}

/***************************************************************************//**
 * Abandon the line on Ctrl-C.
 ******************************************************************************/
static line_edit_event_t abort_line(line_edit_t *le)
{
  static const char marker[] = { '^', 'C' };

  if (le->echo == LINE_EDIT_ECHO_ON) {
    emit(le, marker, (uint16_t)sizeof(marker));
  }
  echo_newline(le);
  clear_line(le);

  return LINE_EDIT_ABORT;
}

sl_status_t line_edit_init(line_edit_t *le,
                           char *buf,
                           uint16_t cap,
                           line_edit_echo_fn_t echo_fn,
                           void *user)
{
  if ((le == NULL) || (buf == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }
  if (cap < LINE_EDIT_MIN_CAP) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  le->buf = buf;
  le->cap = cap;
  le->len = 0U;
  le->echo = LINE_EDIT_ECHO_ON;
  le->echo_fn = echo_fn;
  le->user = user;
  le->overflowed = false;
  le->pending_lf = false;
  le->esc = LINE_EDIT_ESC_NONE;
  le->buf[0] = '\0';

  return SL_STATUS_OK;
}

sl_status_t line_edit_set_echo(line_edit_t *le, line_edit_echo_mode_t mode)
{
  if (le == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if ((mode != LINE_EDIT_ECHO_ON)
      && (mode != LINE_EDIT_ECHO_OFF)
      && (mode != LINE_EDIT_ECHO_MASK)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  le->echo = mode;

  return SL_STATUS_OK;
}

line_edit_event_t line_edit_feed(line_edit_t *le, char c)
{
  uint8_t byte = (uint8_t)c;
  bool was_pending_lf;

  if (le == NULL) {
    return LINE_EDIT_NONE;
  }

  if (consume_escape(le, c)) {
    return LINE_EDIT_NONE;
  }

  // A CR may be followed by an LF that belongs to the same line ending. Clear
  // the flag on whatever arrives next, so that a lone LF later on still ends a
  // line of its own.
  was_pending_lf = le->pending_lf;
  le->pending_lf = false;

  switch (c) {
    case LINE_EDIT_CH_CR:
      le->pending_lf = true;
      return finish_line(le);

    case LINE_EDIT_CH_LF:
      if (was_pending_lf) {
        return LINE_EDIT_NONE;
      }
      return finish_line(le);

    case LINE_EDIT_CH_BS:
    case LINE_EDIT_CH_DEL:
      return erase_char(le);

    case LINE_EDIT_CH_ETX:
      return abort_line(le);

    case LINE_EDIT_CH_ESC:
      le->esc = LINE_EDIT_ESC_GOT_ESC;
      return LINE_EDIT_NONE;

    case '?':
      if (le->echo == LINE_EDIT_ECHO_ON) {
        return LINE_EDIT_HELP;
      }
      return append_char(le, c);

    default:
      break;
  }

  if ((byte >= 0x20U) && (byte <= 0x7EU)) {
    return append_char(le, c);
  }

  // Any other control character: no meaning here, and echoing it would move the
  // cursor somewhere unexpected.
  return LINE_EDIT_NONE;
}

const char *line_edit_line(const line_edit_t *le)
{
  if ((le == NULL) || (le->buf == NULL)) {
    return "";
  }

  return le->buf;
}

uint16_t line_edit_len(const line_edit_t *le)
{
  if (le == NULL) {
    return 0U;
  }

  return le->len;
}

sl_status_t line_edit_reset(line_edit_t *le)
{
  if (le == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  clear_line(le);

  return SL_STATUS_OK;
}
