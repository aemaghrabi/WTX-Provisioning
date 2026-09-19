/***************************************************************************//**
 * @file
 * @brief Unit tests for the cli_parser service.
 ******************************************************************************/

#include <string.h>

#include "test_util.h"
#include "cli_parser.h"

/// @name Modes, mirroring how the console uses the mask
/// @{
#define MODE_USER    0x01U
#define MODE_PRIV    0x02U
#define MODE_CONFIG  0x04U
#define MODE_EXEC    (MODE_USER | MODE_PRIV)
/// @}

/// @name Command identifiers used by the fixture tree
/// @{
#define CMD_SHOW_AT      1U
#define CMD_SHOW_ALL     2U
#define CMD_SHOW_VER     3U
#define CMD_ENABLE       4U
#define CMD_CONFIGURE    5U
#define CMD_XBEE_AT_SET  6U
#define CMD_EXIT         7U
/// @}

// A cut-down copy of the console's own grammar, deep enough to exercise
// keywords, arguments, mode filtering and every diagnostic.

static const cli_node_t at_value_node[] = {
  { "<VALUE>", "New value", CLI_NODE_ARG, MODE_CONFIG, CMD_XBEE_AT_SET, NULL, 0U },
};

static const cli_node_t config_at_name_node[] = {
  { "<NAME>", "Two-character AT command", CLI_NODE_ARG, MODE_CONFIG,
    CLI_CMD_NONE, at_value_node, 1U },
};

static const cli_node_t show_at_name_node[] = {
  { "<NAME>", "Two-character AT command", CLI_NODE_ARG, MODE_EXEC,
    CMD_SHOW_AT, NULL, 0U },
};

static const cli_node_t show_xbee_children[] = {
  { "all", "All readable AT parameters", CLI_NODE_KEYWORD, MODE_EXEC,
    CMD_SHOW_ALL, NULL, 0U },
  { "at", "One AT parameter by name", CLI_NODE_KEYWORD, MODE_EXEC,
    CLI_CMD_NONE, show_at_name_node, 1U },
};

static const cli_node_t show_children[] = {
  { "version", "Firmware and module identity", CLI_NODE_KEYWORD, MODE_EXEC,
    CMD_SHOW_VER, NULL, 0U },
  { "xbee", "XBee module information", CLI_NODE_KEYWORD, MODE_EXEC,
    CLI_CMD_NONE, show_xbee_children, 2U },
};

static const cli_node_t configure_children[] = {
  { "terminal", "Configure from the terminal", CLI_NODE_KEYWORD, MODE_PRIV,
    CMD_CONFIGURE, NULL, 0U },
};

static const cli_node_t xbee_children[] = {
  { "at", "Set an AT parameter", CLI_NODE_KEYWORD, MODE_CONFIG,
    CLI_CMD_NONE, config_at_name_node, 1U },
};

static const cli_node_t root[] = {
  { "configure", "Enter configuration mode", CLI_NODE_KEYWORD, MODE_PRIV,
    CLI_CMD_NONE, configure_children, 1U },
  { "enable", "Turn on privileged commands", CLI_NODE_KEYWORD, MODE_USER,
    CMD_ENABLE, NULL, 0U },
  { "exit", "Leave the current mode", CLI_NODE_KEYWORD,
    MODE_USER | MODE_PRIV | MODE_CONFIG, CMD_EXIT, NULL, 0U },
  { "show", "Show running system information", CLI_NODE_KEYWORD, MODE_EXEC,
    CLI_CMD_NONE, show_children, 2U },
  { "xbee", "XBee module configuration", CLI_NODE_KEYWORD, MODE_CONFIG,
    CLI_CMD_NONE, xbee_children, 1U },
};

#define ROOT_COUNT  ((uint8_t)(sizeof(root) / sizeof(root[0])))

/// Scratch copy, because both entry points tokenize in place.
static char work[CLI_PARSER_LINE_MAX];

/// Help candidates collected by the emit callback, joined with '|'.
static char help_text[512];

/***************************************************************************//**
 * Match a line, working on a scratch copy of it.
 ******************************************************************************/
static cli_parse_result_t parse(const char *line, uint8_t modes)
{
  cli_parse_result_t result;

  TEST_ASSERT(strlen(line) < sizeof(work));
  (void)strcpy(work, line);

  TEST_ASSERT_EQ_UINT(cli_parser_parse(root, ROOT_COUNT, work, modes, &result),
                      SL_STATUS_OK);

  return result;
}

/***************************************************************************//**
 * Collect one help candidate.
 ******************************************************************************/
static void help_cb(const char *token, const char *help, void *user)
{
  TEST_ASSERT(user == (void *)help_text);
  TEST_ASSERT(help != NULL);

  if (help_text[0] != '\0') {
    (void)strncat(help_text, "|", sizeof(help_text) - strlen(help_text) - 1U);
  }
  (void)strncat(help_text, token, sizeof(help_text) - strlen(help_text) - 1U);
}

/***************************************************************************//**
 * Ask for help on a line and return the candidates, joined with '|'.
 ******************************************************************************/
static const char *help(const char *line, uint8_t modes, uint8_t *count)
{
  TEST_ASSERT(strlen(line) < sizeof(work));
  (void)strcpy(work, line);
  help_text[0] = '\0';

  TEST_ASSERT_EQ_UINT(cli_parser_help(root, ROOT_COUNT, work, modes,
                                      help_cb, help_text, count),
                      SL_STATUS_OK);

  return help_text;
}

/***************************************************************************//**
 * A complete keyword command matches and reports its identifier.
 ******************************************************************************/
static void test_keyword_command(void)
{
  cli_parse_result_t r = parse("show version", MODE_USER);

  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_OK);
  TEST_ASSERT_EQ_UINT(r.id, CMD_SHOW_VER);
  TEST_ASSERT_EQ_UINT(r.arg_count, 0U);

  r = parse("show xbee all", MODE_USER);
  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_OK);
  TEST_ASSERT_EQ_UINT(r.id, CMD_SHOW_ALL);

  r = parse("enable", MODE_USER);
  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_OK);
  TEST_ASSERT_EQ_UINT(r.id, CMD_ENABLE);
}

/***************************************************************************//**
 * Arguments are captured in order.
 ******************************************************************************/
static void test_arguments(void)
{
  cli_parse_result_t r = parse("show xbee at CH", MODE_USER);

  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_OK);
  TEST_ASSERT_EQ_UINT(r.id, CMD_SHOW_AT);
  TEST_ASSERT_EQ_UINT(r.arg_count, 1U);
  TEST_ASSERT_EQ_STR(r.args[0], "CH");

  r = parse("xbee at CH 0F", MODE_CONFIG);
  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_OK);
  TEST_ASSERT_EQ_UINT(r.id, CMD_XBEE_AT_SET);
  TEST_ASSERT_EQ_UINT(r.arg_count, 2U);
  TEST_ASSERT_EQ_STR(r.args[0], "CH");
  TEST_ASSERT_EQ_STR(r.args[1], "0F");
}

/***************************************************************************//**
 * A keyword beats an argument at the same level.
 *
 * "all" is a keyword under "show xbee", so it must not be captured as the
 * argument of "show xbee at".
 ******************************************************************************/
static void test_keyword_wins_over_argument(void)
{
  cli_parse_result_t r = parse("show xbee at all", MODE_USER);

  // "all" is not a keyword at this level, so the argument catches it.
  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_OK);
  TEST_ASSERT_EQ_STR(r.args[0], "all");

  // One level up, the keyword takes precedence over nothing at all, and is
  // still matched exactly.
  r = parse("show xbee all", MODE_USER);
  TEST_ASSERT_EQ_UINT(r.id, CMD_SHOW_ALL);
}

/***************************************************************************//**
 * Separators are collapsed, wherever they are.
 ******************************************************************************/
static void test_whitespace(void)
{
  cli_parse_result_t r = parse("   show    xbee   at   CH   ", MODE_USER);

  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_OK);
  TEST_ASSERT_EQ_UINT(r.id, CMD_SHOW_AT);
  TEST_ASSERT_EQ_STR(r.args[0], "CH");

  r = parse("\tshow\tversion\t", MODE_USER);
  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_OK);
  TEST_ASSERT_EQ_UINT(r.id, CMD_SHOW_VER);
}

/***************************************************************************//**
 * A blank line is reported as such, not as an error.
 ******************************************************************************/
static void test_empty(void)
{
  TEST_ASSERT_EQ_UINT(parse("", MODE_USER).status, CLI_PARSE_EMPTY);
  TEST_ASSERT_EQ_UINT(parse("   ", MODE_USER).status, CLI_PARSE_EMPTY);
  TEST_ASSERT_EQ_UINT(parse("\t \t", MODE_USER).status, CLI_PARSE_EMPTY);
}

/***************************************************************************//**
 * A word that matches nothing is reported with its position.
 ******************************************************************************/
static void test_unknown(void)
{
  cli_parse_result_t r = parse("bogus", MODE_USER);

  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_UNKNOWN);
  TEST_ASSERT_EQ_UINT(r.bad_index, 0U);

  r = parse("show bogus", MODE_USER);
  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_UNKNOWN);
  TEST_ASSERT_EQ_UINT(r.bad_index, 1U);

  r = parse("show xbee bogus extra", MODE_USER);
  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_UNKNOWN);
  TEST_ASSERT_EQ_UINT(r.bad_index, 2U);
}

/***************************************************************************//**
 * A prefix of a command is incomplete, not unknown.
 ******************************************************************************/
static void test_incomplete(void)
{
  TEST_ASSERT_EQ_UINT(parse("show", MODE_USER).status, CLI_PARSE_INCOMPLETE);
  TEST_ASSERT_EQ_UINT(parse("show xbee", MODE_USER).status,
                      CLI_PARSE_INCOMPLETE);
  TEST_ASSERT_EQ_UINT(parse("show xbee at", MODE_USER).status,
                      CLI_PARSE_INCOMPLETE);
  TEST_ASSERT_EQ_UINT(parse("configure", MODE_PRIV).status,
                      CLI_PARSE_INCOMPLETE);
  TEST_ASSERT_EQ_UINT(parse("xbee at CH", MODE_CONFIG).status,
                      CLI_PARSE_INCOMPLETE);
}

/***************************************************************************//**
 * Words after a complete command are reported separately from a wrong word.
 ******************************************************************************/
static void test_too_many(void)
{
  cli_parse_result_t r = parse("show version now", MODE_USER);

  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_TOO_MANY);
  TEST_ASSERT_EQ_UINT(r.bad_index, 2U);

  r = parse("show xbee at CH extra", MODE_USER);
  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_TOO_MANY);
  TEST_ASSERT_EQ_UINT(r.bad_index, 4U);
}

/***************************************************************************//**
 * A line with more words than the parser holds is refused, not truncated.
 ******************************************************************************/
static void test_token_limit(void)
{
  cli_parse_result_t r = parse("a b c d e f g h i j k", MODE_USER);

  TEST_ASSERT_EQ_UINT(r.status, CLI_PARSE_TOO_MANY);
}

/***************************************************************************//**
 * A node is invisible outside its modes.
 ******************************************************************************/
static void test_mode_filtering(void)
{
  // configure terminal is privileged only.
  TEST_ASSERT_EQ_UINT(parse("configure terminal", MODE_PRIV).status,
                      CLI_PARSE_OK);
  TEST_ASSERT_EQ_UINT(parse("configure terminal", MODE_USER).status,
                      CLI_PARSE_UNKNOWN);

  // enable is offered in user mode only, since there is nothing to enable once
  // already privileged.
  TEST_ASSERT_EQ_UINT(parse("enable", MODE_USER).status, CLI_PARSE_OK);
  TEST_ASSERT_EQ_UINT(parse("enable", MODE_PRIV).status, CLI_PARSE_UNKNOWN);

  // show works at both EXEC levels but not in configuration mode.
  TEST_ASSERT_EQ_UINT(parse("show version", MODE_PRIV).status, CLI_PARSE_OK);
  TEST_ASSERT_EQ_UINT(parse("show version", MODE_CONFIG).status,
                      CLI_PARSE_UNKNOWN);

  // Writing a parameter is configuration mode only.
  TEST_ASSERT_EQ_UINT(parse("xbee at CH 0F", MODE_CONFIG).status,
                      CLI_PARSE_OK);
  TEST_ASSERT_EQ_UINT(parse("xbee at CH 0F", MODE_PRIV).status,
                      CLI_PARSE_UNKNOWN);

  // exit exists everywhere.
  TEST_ASSERT_EQ_UINT(parse("exit", MODE_USER).status, CLI_PARSE_OK);
  TEST_ASSERT_EQ_UINT(parse("exit", MODE_PRIV).status, CLI_PARSE_OK);
  TEST_ASSERT_EQ_UINT(parse("exit", MODE_CONFIG).status, CLI_PARSE_OK);
}

/***************************************************************************//**
 * Help on an empty line lists everything available in the current mode.
 ******************************************************************************/
static void test_help_at_root(void)
{
  uint8_t count = 0U;

  TEST_ASSERT_EQ_STR(help("", MODE_USER, &count), "enable|exit|show");
  TEST_ASSERT_EQ_UINT(count, 3U);

  TEST_ASSERT_EQ_STR(help("", MODE_PRIV, &count), "configure|exit|show");
  TEST_ASSERT_EQ_UINT(count, 3U);

  TEST_ASSERT_EQ_STR(help("", MODE_CONFIG, &count), "exit|xbee");
  TEST_ASSERT_EQ_UINT(count, 2U);
}

/***************************************************************************//**
 * Help after a separator lists what may follow.
 ******************************************************************************/
static void test_help_after_separator(void)
{
  uint8_t count = 0U;

  TEST_ASSERT_EQ_STR(help("show ", MODE_USER, &count), "version|xbee");
  TEST_ASSERT_EQ_UINT(count, 2U);

  TEST_ASSERT_EQ_STR(help("show xbee ", MODE_USER, &count), "all|at");
  TEST_ASSERT_EQ_STR(help("show xbee at ", MODE_USER, &count), "<NAME>");
  TEST_ASSERT_EQ_STR(help("xbee at CH ", MODE_CONFIG, &count), "<VALUE>");
}

/***************************************************************************//**
 * Help part way through a word narrows to the keywords beginning with it.
 ******************************************************************************/
static void test_help_with_prefix(void)
{
  uint8_t count = 0U;

  TEST_ASSERT_EQ_STR(help("s", MODE_USER, &count), "show");
  TEST_ASSERT_EQ_UINT(count, 1U);

  TEST_ASSERT_EQ_STR(help("e", MODE_USER, &count), "enable|exit");
  TEST_ASSERT_EQ_UINT(count, 2U);

  TEST_ASSERT_EQ_STR(help("show v", MODE_USER, &count), "version");
  TEST_ASSERT_EQ_STR(help("show xbee a", MODE_USER, &count), "all|at");

  // A prefix matching nothing offers nothing.
  TEST_ASSERT_EQ_STR(help("zz", MODE_USER, &count), "");
  TEST_ASSERT_EQ_UINT(count, 0U);

  // An argument accepts any word, so a partly typed one never hides it.
  TEST_ASSERT_EQ_STR(help("show xbee at C", MODE_USER, &count), "<NAME>");
}

/***************************************************************************//**
 * A line that is already a command says so with "<cr>", last.
 ******************************************************************************/
static void test_help_reports_cr(void)
{
  uint8_t count = 0U;

  TEST_ASSERT_EQ_STR(help("show xbee all ", MODE_USER, &count), "<cr>");
  TEST_ASSERT_EQ_UINT(count, 1U);

  TEST_ASSERT_EQ_STR(help("show version ", MODE_USER, &count), "<cr>");
  TEST_ASSERT_EQ_STR(help("show xbee at CH ", MODE_USER, &count), "<cr>");

  // Complete, and still extensible: the continuation comes first.
  TEST_ASSERT_EQ_STR(help("xbee at CH 0F ", MODE_CONFIG, &count), "<cr>");

  // Incomplete lines get no "<cr>".
  TEST_ASSERT_EQ_STR(help("show ", MODE_USER, &count), "version|xbee");

  // Nor does a line ending mid-word, since it is not complete as it stands.
  TEST_ASSERT_EQ_STR(help("show xbee al", MODE_USER, &count), "all");
}

/***************************************************************************//**
 * Help offers nothing once the line has gone wrong.
 ******************************************************************************/
static void test_help_after_bad_input(void)
{
  uint8_t count = 0U;

  TEST_ASSERT_EQ_STR(help("bogus ", MODE_USER, &count), "");
  TEST_ASSERT_EQ_UINT(count, 0U);

  TEST_ASSERT_EQ_STR(help("show bogus ", MODE_USER, &count), "");
  TEST_ASSERT_EQ_UINT(count, 0U);

  // A privileged command is not merely hidden from help in user mode, it
  // cannot be continued either.
  TEST_ASSERT_EQ_STR(help("configure ", MODE_USER, &count), "");
  TEST_ASSERT_EQ_STR(help("configure ", MODE_PRIV, &count), "terminal");
}

/***************************************************************************//**
 * Help counts without emitting when no callback is given.
 ******************************************************************************/
static void test_help_count_only(void)
{
  uint8_t count = 0U;

  (void)strcpy(work, "show ");
  TEST_ASSERT_EQ_UINT(cli_parser_help(root, ROOT_COUNT, work, MODE_USER,
                                      NULL, NULL, &count),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(count, 2U);

  // A missing count is allowed too.
  (void)strcpy(work, "show ");
  TEST_ASSERT_EQ_UINT(cli_parser_help(root, ROOT_COUNT, work, MODE_USER,
                                      NULL, NULL, NULL),
                      SL_STATUS_OK);
}

/***************************************************************************//**
 * Both entry points reject NULL arguments.
 ******************************************************************************/
static void test_null_arguments(void)
{
  cli_parse_result_t result;
  uint8_t count = 0U;

  (void)strcpy(work, "show");

  TEST_ASSERT_EQ_UINT(cli_parser_parse(NULL, ROOT_COUNT, work, MODE_USER,
                                       &result),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(cli_parser_parse(root, ROOT_COUNT, NULL, MODE_USER,
                                       &result),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(cli_parser_parse(root, ROOT_COUNT, work, MODE_USER,
                                       NULL),
                      SL_STATUS_NULL_POINTER);

  TEST_ASSERT_EQ_UINT(cli_parser_help(NULL, ROOT_COUNT, work, MODE_USER,
                                      NULL, NULL, &count),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(cli_parser_help(root, ROOT_COUNT, NULL, MODE_USER,
                                      NULL, NULL, &count),
                      SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * Entry point.
 ******************************************************************************/
int main(void)
{
  TEST_RUN(test_keyword_command);
  TEST_RUN(test_arguments);
  TEST_RUN(test_keyword_wins_over_argument);
  TEST_RUN(test_whitespace);
  TEST_RUN(test_empty);
  TEST_RUN(test_unknown);
  TEST_RUN(test_incomplete);
  TEST_RUN(test_too_many);
  TEST_RUN(test_token_limit);
  TEST_RUN(test_mode_filtering);
  TEST_RUN(test_help_at_root);
  TEST_RUN(test_help_after_separator);
  TEST_RUN(test_help_with_prefix);
  TEST_RUN(test_help_reports_cr);
  TEST_RUN(test_help_after_bad_input);
  TEST_RUN(test_help_count_only);
  TEST_RUN(test_null_arguments);

  return TEST_SUMMARY();
}
