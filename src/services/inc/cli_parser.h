/***************************************************************************//**
 * @file
 * @brief Table-driven command matching and context help for a Cisco-style CLI.
 *
 * Holds the grammar as a tree of @ref cli_node_t and answers two questions
 * about a typed line: which command does it name, and what could come next.
 * Both answers come from the same tree, so the help can never describe a
 * command that dispatch does not accept.
 *
 * The module is pure. It does no input or output, knows nothing about the XBee
 * and nothing about the console's modes beyond an opaque bitmask, so it builds
 * and runs under the host tests unchanged.
 *
 * Usage:
 * @code
 * static const cli_node_t show_children[] = { ... };
 * static const cli_node_t root[] = {
 *   { "show", "Show running system information", CLI_NODE_KEYWORD,
 *     MODE_ALL, CLI_CMD_NONE, show_children, 2U },
 * };
 *
 * char work[128];
 * cli_parse_result_t result;
 *
 * (void)strcpy(work, line);
 * (void)cli_parser_parse(root, 1U, work, current_modes, &result);
 * @endcode
 *
 * @note Both entry points tokenize in place, overwriting the separators in the
 *       line they are given. Pass a scratch copy when the original has to
 *       survive, which it does after a help request: the operator carries on
 *       typing the same line.
 ******************************************************************************/

#ifndef CLI_PARSER_H
#define CLI_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sl_status.h"

/// Longest command line the parser will tokenize, terminator included.
#ifndef CLI_PARSER_LINE_MAX
#define CLI_PARSER_LINE_MAX   128U
#endif

/// Most words one line may hold.
#ifndef CLI_PARSER_MAX_TOKENS
#define CLI_PARSER_MAX_TOKENS  8U
#endif

/// Most arguments one command may take.
#ifndef CLI_PARSER_MAX_ARGS
#define CLI_PARSER_MAX_ARGS    4U
#endif

/// Command identifier meaning "this node does not complete a command".
///
/// A node carrying it can still be walked through to reach its children, which
/// is what makes "show" a step towards "show version" without being a command
/// in its own right.
#define CLI_CMD_NONE  0U

/// What a node matches.
typedef enum {
  CLI_NODE_KEYWORD,  ///< A literal word, which must be typed in full.
  CLI_NODE_ARG,      ///< Any single word, captured as an argument.
} cli_node_kind_t;

/// One node of the command tree.
typedef struct cli_node cli_node_t;

struct cli_node {
  /// Keyword text, or the placeholder shown in help for an argument, such as
  /// "<NAME>". Never NULL.
  const char *token;
  /// One-line description, shown by '?'. Never NULL.
  const char *help;
  /// A @ref cli_node_kind_t value.
  uint8_t kind;
  /// Bitmask of the caller's modes in which this node exists. The parser only
  /// tests it against the mask given to it, and attaches no meaning to the bits.
  uint8_t modes;
  /// Identifier reported when the line ends here, or @ref CLI_CMD_NONE.
  uint16_t id;
  /// Child nodes, or NULL.
  const cli_node_t *children;
  /// Number of entries in children.
  uint8_t child_count;
};

/// Outcome of matching a line against the tree.
typedef enum {
  CLI_PARSE_OK,          ///< A complete command matched.
  CLI_PARSE_EMPTY,       ///< The line held no words.
  CLI_PARSE_UNKNOWN,     ///< A word matches nothing available here.
  CLI_PARSE_INCOMPLETE,  ///< The line names a prefix of a command, not a command.
  CLI_PARSE_TOO_MANY,    ///< More words than the command, or the parser, accepts.
} cli_parse_status_t;

/// What a line was found to mean.
typedef struct {
  cli_parse_status_t status;                 ///< Outcome.
  uint16_t           id;                     ///< Command identifier, valid when status is CLI_PARSE_OK.
  const char        *args[CLI_PARSER_MAX_ARGS];  ///< Captured arguments, in order.
  uint8_t            arg_count;              ///< Number of arguments captured.
  /// Zero-based index of the word at fault, for a caret under the offending
  /// word as IOS prints it. Valid when status is CLI_PARSE_UNKNOWN or
  /// CLI_PARSE_TOO_MANY.
  uint8_t            bad_index;
} cli_parse_result_t;

/***************************************************************************//**
 * Report one help candidate.
 *
 * Called once per possible continuation, in tree order. The parser formats
 * nothing itself, so that the console owns the column layout and the output
 * path stays out of the host tests.
 *
 * @param[in] token The keyword, the argument placeholder, or "<cr>" to say that
 *                  the line is already a complete command.
 * @param[in] help  Its description. Empty for "<cr>". Never NULL.
 * @param[in] user  Context given to @ref cli_parser_help.
 ******************************************************************************/
typedef void (*cli_help_fn_t)(const char *token, const char *help, void *user);

/***************************************************************************//**
 * Match a line against the command tree.
 *
 * A keyword must be typed in full: this console has no abbreviation, so a word
 * either equals a keyword or does not match it. Where a level offers both
 * keywords and an argument, a keyword wins, and the argument catches
 * everything else.
 *
 * @param[in]  root       Top-level nodes.
 * @param[in]  root_count Number of them.
 * @param[in,out] line    Line to match. Tokenized in place.
 * @param[in]  modes      Mask of the caller's current modes. A node is
 *                        invisible unless it shares a bit with this.
 * @param[out] result     What the line means.
 *
 * @return SL_STATUS_OK when the line was matched or diagnosed, which includes
 *             every value of @ref cli_parse_status_t,
 *         SL_STATUS_NULL_POINTER if root, line or result is NULL.
 ******************************************************************************/
sl_status_t cli_parser_parse(const cli_node_t *root,
                             uint8_t root_count,
                             char *line,
                             uint8_t modes,
                             cli_parse_result_t *result);

/***************************************************************************//**
 * List what could follow the line typed so far.
 *
 * Where the line ends decides what is offered. Ending on a separator, or on
 * nothing at all, offers every node available at that point. Ending part way
 * through a word offers the keywords that begin with it, which is how '?'
 * narrows as the operator types.
 *
 * "<cr>" is reported, last, when the line as it stands is already a complete
 * command.
 *
 * @param[in]  root       Top-level nodes.
 * @param[in]  root_count Number of them.
 * @param[in,out] line    Line typed so far. Tokenized in place.
 * @param[in]  modes      Mask of the caller's current modes.
 * @param[in]  emit       Called once per candidate. May be NULL to count only.
 * @param[in]  user       Context for emit.
 * @param[out] count      Number of candidates reported. May be NULL. Zero means
 *                        the line cannot be continued into any command.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if root or line is NULL.
 ******************************************************************************/
sl_status_t cli_parser_help(const cli_node_t *root,
                            uint8_t root_count,
                            char *line,
                            uint8_t modes,
                            cli_help_fn_t emit,
                            void *user,
                            uint8_t *count);

#endif  // CLI_PARSER_H
