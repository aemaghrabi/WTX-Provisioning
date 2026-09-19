/***************************************************************************//**
 * @file
 * @brief Unit tests for the line_edit utility.
 ******************************************************************************/

#include "test_util.h"
#include "line_edit.h"

/// Capacity used by most cases. Holds CAP - 1 characters.
#define CAP  16U

static char storage[CAP];
static line_edit_t le;

/// Everything the echo callback has emitted since the last fixture reset.
static char echoed[256];
static uint16_t echoed_len;

/***************************************************************************//**
 * Echo sink. Records what the editor would have sent to the terminal.
 ******************************************************************************/
static void echo_cb(const char *text, uint16_t len, void *user)
{
  uint16_t i;

  TEST_ASSERT(user == (void *)&le);

  for (i = 0U; i < len; i++) {
    if (echoed_len < (uint16_t)(sizeof(echoed) - 1U)) {
      echoed[echoed_len] = text[i];
      echoed_len++;
    }
  }
  echoed[echoed_len] = '\0';
}

/***************************************************************************//**
 * Reset the fixture to an empty editor with echo on.
 ******************************************************************************/
static void fixture_reset(void)
{
  echoed_len = 0U;
  echoed[0] = '\0';
  TEST_ASSERT_EQ_UINT(line_edit_init(&le, storage, CAP, echo_cb, &le),
                      SL_STATUS_OK);
}

/***************************************************************************//**
 * Feed a string, asserting that every byte reports LINE_EDIT_NONE.
 ******************************************************************************/
static void feed_quiet(const char *text)
{
  uint16_t i;

  for (i = 0U; text[i] != '\0'; i++) {
    line_edit_event_t ev = line_edit_feed(&le, text[i]);

    if (ev != LINE_EDIT_NONE) {
      TEST_FAIL("byte %u ('%c') reported %d, expected LINE_EDIT_NONE",
                (unsigned)i, text[i], (int)ev);
    }
  }
}

/***************************************************************************//**
 * Init rejects bad arguments.
 ******************************************************************************/
static void test_init_validation(void)
{
  TEST_ASSERT_EQ_UINT(line_edit_init(NULL, storage, CAP, NULL, NULL),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(line_edit_init(&le, NULL, CAP, NULL, NULL),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(line_edit_init(&le, storage, 1U, NULL, NULL),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(line_edit_init(&le, storage, 2U, NULL, NULL),
                      SL_STATUS_OK);

  // A fresh editor holds an empty, terminated line.
  TEST_ASSERT_EQ_UINT(line_edit_len(&le), 0U);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "");
}

/***************************************************************************//**
 * A typed line is assembled, echoed and reported once on CR.
 ******************************************************************************/
static void test_simple_line(void)
{
  fixture_reset();

  feed_quiet("show");
  TEST_ASSERT_EQ_UINT(line_edit_len(&le), 4U);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "show");
  TEST_ASSERT_EQ_STR(echoed, "show");

  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\r'), LINE_EDIT_READY);
  // The line survives until the caller resets, so it can be dispatched in place.
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "show");
  TEST_ASSERT_EQ_STR(echoed, "show\n");

  TEST_ASSERT_EQ_UINT(line_edit_reset(&le), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(line_edit_len(&le), 0U);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "");
}

/***************************************************************************//**
 * An empty line still reports ready, which is what reprints the prompt.
 ******************************************************************************/
static void test_empty_line(void)
{
  fixture_reset();

  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\r'), LINE_EDIT_READY);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "");
  TEST_ASSERT_EQ_STR(echoed, "\n");
}

/***************************************************************************//**
 * CR, LF and CRLF each end exactly one line.
 *
 * The CRLF case is the one that matters: the caller resets between the CR and
 * the LF, and the LF must not then end a second, empty line.
 ******************************************************************************/
static void test_line_endings(void)
{
  // Lone LF.
  fixture_reset();
  feed_quiet("a");
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\n'), LINE_EDIT_READY);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "a");

  // CRLF, with the reset in between, as the console does it.
  fixture_reset();
  feed_quiet("b");
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\r'), LINE_EDIT_READY);
  TEST_ASSERT_EQ_UINT(line_edit_reset(&le), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\n'), LINE_EDIT_NONE);

  // CRLF without a reset.
  fixture_reset();
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\r'), LINE_EDIT_READY);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\n'), LINE_EDIT_NONE);

  // LF then CR is two separate endings, not one: only an LF directly after a
  // CR is part of the same terminator.
  fixture_reset();
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\n'), LINE_EDIT_READY);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\r'), LINE_EDIT_READY);

  // A character between the CR and the LF clears the pairing.
  fixture_reset();
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\r'), LINE_EDIT_READY);
  feed_quiet("x");
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\n'), LINE_EDIT_READY);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "x");
}

/***************************************************************************//**
 * Backspace and DEL each erase one character and repaint the terminal.
 ******************************************************************************/
static void test_backspace(void)
{
  fixture_reset();

  feed_quiet("abc");
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x08'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "ab");
  TEST_ASSERT_EQ_UINT(line_edit_len(&le), 2U);
  TEST_ASSERT_EQ_STR(echoed, "abc\b \b");

  // DEL erases just the same, because terminals disagree about which they send.
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x7F'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "a");
}

/***************************************************************************//**
 * Backspace at column zero does nothing and echoes nothing.
 *
 * Echoing would walk the cursor back over the prompt.
 ******************************************************************************/
static void test_backspace_at_column_zero(void)
{
  fixture_reset();

  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x08'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x7F'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_UINT(line_edit_len(&le), 0U);
  TEST_ASSERT_EQ_STR(echoed, "");

  // Typing then erasing back to empty leaves the editor usable.
  feed_quiet("a");
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x08'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x08'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_UINT(line_edit_len(&le), 0U);
  feed_quiet("b");
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "b");
}

/***************************************************************************//**
 * Ctrl-C abandons the line and marks it on the terminal.
 ******************************************************************************/
static void test_abort(void)
{
  fixture_reset();

  feed_quiet("show xbee");
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x03'), LINE_EDIT_ABORT);
  TEST_ASSERT_EQ_UINT(line_edit_len(&le), 0U);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "");
  TEST_ASSERT_EQ_STR(echoed, "show xbee^C\n");

  // The editor is immediately usable again.
  feed_quiet("ab");
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "ab");
}

/***************************************************************************//**
 * '?' asks for help without disturbing the line.
 ******************************************************************************/
static void test_help(void)
{
  fixture_reset();

  feed_quiet("show ");
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '?'), LINE_EDIT_HELP);
  // The line is untouched, so the caller can inspect it and the operator can
  // carry on typing where they left off.
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "show ");
  TEST_ASSERT_EQ_UINT(line_edit_len(&le), 5U);
  // The '?' itself is not echoed; the caller repaints the line after the help.
  TEST_ASSERT_EQ_STR(echoed, "show ");

  feed_quiet("xbee");
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "show xbee");

  // Help on an empty line works too.
  fixture_reset();
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '?'), LINE_EDIT_HELP);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "");
}

/***************************************************************************//**
 * An over-long line is reported once and its tail is swallowed.
 *
 * The terminator must not report ready: that would hand the caller the remains
 * of a command it has already rejected.
 ******************************************************************************/
static void test_overflow(void)
{
  uint16_t i;

  fixture_reset();

  // CAP - 1 characters fit exactly.
  for (i = 0U; i < (CAP - 1U); i++) {
    TEST_ASSERT_EQ_UINT(line_edit_feed(&le, 'x'), LINE_EDIT_NONE);
  }
  TEST_ASSERT_EQ_UINT(line_edit_len(&le), CAP - 1U);

  // The next one does not.
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, 'y'), LINE_EDIT_OVERFLOW);
  TEST_ASSERT_EQ_UINT(line_edit_len(&le), 0U);

  // Reported once only, and the rest of the line is swallowed.
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, 'z'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x08'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_UINT(line_edit_len(&le), 0U);

  // The terminator closes the overflowed line without dispatching it.
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\r'), LINE_EDIT_NONE);

  // The next line is ordinary again.
  feed_quiet("ok");
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\r'), LINE_EDIT_READY);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "ok");
}

/***************************************************************************//**
 * Ctrl-C clears an overflowed line as well.
 ******************************************************************************/
static void test_overflow_then_abort(void)
{
  uint16_t i;

  fixture_reset();

  for (i = 0U; i < CAP; i++) {
    (void)line_edit_feed(&le, 'x');
  }
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x03'), LINE_EDIT_ABORT);

  feed_quiet("ab");
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\r'), LINE_EDIT_READY);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "ab");
}

/***************************************************************************//**
 * Masked echo hides a password without changing what is collected.
 ******************************************************************************/
static void test_echo_modes(void)
{
  fixture_reset();

  TEST_ASSERT_EQ_UINT(line_edit_set_echo(&le, LINE_EDIT_ECHO_MASK),
                      SL_STATUS_OK);
  feed_quiet("admin");
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "admin");
  TEST_ASSERT_EQ_STR(echoed, "*****");

  // Erasing hides itself too, but still shortens the line.
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x08'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "admi");
  TEST_ASSERT_EQ_STR(echoed, "*****\b \b");

  // Silent mode echoes nothing at all.
  fixture_reset();
  TEST_ASSERT_EQ_UINT(line_edit_set_echo(&le, LINE_EDIT_ECHO_OFF),
                      SL_STATUS_OK);
  feed_quiet("secret");
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x08'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "secre");
  TEST_ASSERT_EQ_STR(echoed, "");

  TEST_ASSERT_EQ_UINT(line_edit_set_echo(NULL, LINE_EDIT_ECHO_ON),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(line_edit_set_echo(&le, (line_edit_echo_mode_t)99),
                      SL_STATUS_INVALID_PARAMETER);
}

/***************************************************************************//**
 * '?' is a literal character while a password is being read.
 ******************************************************************************/
static void test_help_is_literal_in_password(void)
{
  fixture_reset();

  TEST_ASSERT_EQ_UINT(line_edit_set_echo(&le, LINE_EDIT_ECHO_MASK),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, 'a'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '?'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "a?");

  fixture_reset();
  TEST_ASSERT_EQ_UINT(line_edit_set_echo(&le, LINE_EDIT_ECHO_OFF),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '?'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "?");
}

/***************************************************************************//**
 * Escape sequences are swallowed whole, not dropped into the line.
 ******************************************************************************/
static void test_escape_sequences(void)
{
  fixture_reset();

  feed_quiet("ab");

  // Up arrow: ESC [ A.
  feed_quiet("\x1B[A");
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "ab");

  // Home on an SS3 keypad: ESC O H.
  feed_quiet("\x1BOH");
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "ab");

  // Delete key: ESC [ 3 ~, four bytes. A fixed three-byte swallow would leak
  // the '~' into the line.
  feed_quiet("\x1B[3~");
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "ab");

  // A two-byte sequence, ESC followed by something that is not an introducer.
  // Split so that the hex escape cannot swallow the letter after it.
  feed_quiet("\x1B" "b");
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "ab");

  // Typing carries on normally afterwards.
  feed_quiet("c");
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\r'), LINE_EDIT_READY);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "abc");
  TEST_ASSERT_EQ_STR(echoed, "abc\n");
}

/***************************************************************************//**
 * Control characters with no meaning here are ignored, not echoed.
 ******************************************************************************/
static void test_other_control_characters(void)
{
  fixture_reset();

  feed_quiet("a");
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\t'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x01'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x00'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "a");
  TEST_ASSERT_EQ_STR(echoed, "a");
}

/***************************************************************************//**
 * An editor with no echo sink still assembles lines.
 ******************************************************************************/
static void test_without_echo_callback(void)
{
  TEST_ASSERT_EQ_UINT(line_edit_init(&le, storage, CAP, NULL, NULL),
                      SL_STATUS_OK);

  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, 'a'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\x08'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, 'b'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_UINT(line_edit_feed(&le, '\r'), LINE_EDIT_READY);
  TEST_ASSERT_EQ_STR(line_edit_line(&le), "b");
}

/***************************************************************************//**
 * Every entry point tolerates a NULL editor.
 ******************************************************************************/
static void test_null_arguments(void)
{
  TEST_ASSERT_EQ_UINT(line_edit_feed(NULL, 'a'), LINE_EDIT_NONE);
  TEST_ASSERT_EQ_STR(line_edit_line(NULL), "");
  TEST_ASSERT_EQ_UINT(line_edit_len(NULL), 0U);
  TEST_ASSERT_EQ_UINT(line_edit_reset(NULL), SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * Entry point.
 ******************************************************************************/
int main(void)
{
  TEST_RUN(test_init_validation);
  TEST_RUN(test_simple_line);
  TEST_RUN(test_empty_line);
  TEST_RUN(test_line_endings);
  TEST_RUN(test_backspace);
  TEST_RUN(test_backspace_at_column_zero);
  TEST_RUN(test_abort);
  TEST_RUN(test_help);
  TEST_RUN(test_overflow);
  TEST_RUN(test_overflow_then_abort);
  TEST_RUN(test_echo_modes);
  TEST_RUN(test_help_is_literal_in_password);
  TEST_RUN(test_escape_sequences);
  TEST_RUN(test_other_control_characters);
  TEST_RUN(test_without_echo_callback);
  TEST_RUN(test_null_arguments);

  return TEST_SUMMARY();
}
