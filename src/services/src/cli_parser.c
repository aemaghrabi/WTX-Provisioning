/***************************************************************************//**
 * @file
 * @brief Table-driven command matching and context help for a Cisco-style CLI.
 ******************************************************************************/

#include <string.h>

#include "cli_parser.h"

/// Text reported by help when the line is already a complete command.
#define CLI_PARSER_CR_TOKEN  "<cr>"

/***************************************************************************//**
 * Report whether a character separates words.
 ******************************************************************************/
static bool is_separator(char c)
{
  return ((c == ' ') || (c == '\t'));
}

/***************************************************************************//**
 * Report whether a node is visible in the caller's current modes.
 ******************************************************************************/
static bool is_visible(const cli_node_t *node, uint8_t modes)
{
  return ((node->modes & modes) != 0U);
}

/***************************************************************************//**
 * Split a line into words, in place.
 *
 * Separators are overwritten with terminators, so each token points into the
 * caller's line and needs no storage of its own.
 *
 * @param[in,out] line      Line to split.
 * @param[out]    tokens    Word pointers.
 * @param[in]     max       Capacity of tokens.
 * @param[out]    too_many  Set when the line holds more words than max.
 *
 * @return Number of words found, at most max.
 ******************************************************************************/
static uint8_t tokenize(char *line,
                        char *tokens[],
                        uint8_t max,
                        bool *too_many)
{
  uint8_t count = 0U;
  uint16_t i = 0U;

  *too_many = false;

  for (;;) {
    while (is_separator(line[i])) {
      i++;
    }
    if (line[i] == '\0') {
      break;
    }

    if (count >= max) {
      *too_many = true;
      break;
    }

    tokens[count] = &line[i];
    count++;

    while ((line[i] != '\0') && !is_separator(line[i])) {
      i++;
    }
    if (line[i] != '\0') {
      line[i] = '\0';
      i++;
    }
  }

  return count;
}

/***************************************************************************//**
 * Find the child that a word selects.
 *
 * A keyword must match in full, because this console has no abbreviation. An
 * argument node catches whatever no keyword claimed, so a level offering both
 * resolves a word that happens to read like a keyword in favour of the keyword.
 *
 * @param[in]  children   Nodes at this level, may be NULL.
 * @param[in]  count      Number of them.
 * @param[in]  word       Word to match.
 * @param[in]  modes      Caller's current modes.
 * @param[out] is_arg     Set when the match was an argument node.
 *
 * @return The node, or NULL when nothing matches.
 ******************************************************************************/
static const cli_node_t *match_child(const cli_node_t *children,
                                     uint8_t count,
                                     const char *word,
                                     uint8_t modes,
                                     bool *is_arg)
{
  const cli_node_t *arg = NULL;
  uint8_t i;

  *is_arg = false;

  if (children == NULL) {
    return NULL;
  }

  for (i = 0U; i < count; i++) {
    const cli_node_t *node = &children[i];

    if (!is_visible(node, modes)) {
      continue;
    }

    if (node->kind == CLI_NODE_KEYWORD) {
      if (strcmp(node->token, word) == 0) {
        return node;
      }
    } else if (arg == NULL) {
      arg = node;
    } else {
      // A second argument node at one level would make the grammar ambiguous.
      // Keep the first, which is the one the tree author wrote first.
    }
  }

  if (arg != NULL) {
    *is_arg = true;
  }

  return arg;
}

/***************************************************************************//**
 * Report whether any node at this level is visible.
 ******************************************************************************/
static bool level_has_visible(const cli_node_t *children,
                              uint8_t count,
                              uint8_t modes)
{
  uint8_t i;

  if (children == NULL) {
    return false;
  }

  for (i = 0U; i < count; i++) {
    if (is_visible(&children[i], modes)) {
      return true;
    }
  }

  return false;
}

sl_status_t cli_parser_parse(const cli_node_t *root,
                             uint8_t root_count,
                             char *line,
                             uint8_t modes,
                             cli_parse_result_t *result)
{
  char *tokens[CLI_PARSER_MAX_TOKENS];
  uint8_t ntok;
  uint8_t i;
  bool too_many = false;
  const cli_node_t *level = root;
  uint8_t level_count = root_count;
  const cli_node_t *node = NULL;

  if ((root == NULL) || (line == NULL) || (result == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }

  result->status = CLI_PARSE_EMPTY;
  result->id = CLI_CMD_NONE;
  result->arg_count = 0U;
  result->bad_index = 0U;

  ntok = tokenize(line, tokens, (uint8_t)CLI_PARSER_MAX_TOKENS, &too_many);

  if (too_many) {
    result->status = CLI_PARSE_TOO_MANY;
    result->bad_index = (uint8_t)(CLI_PARSER_MAX_TOKENS - 1U);
    return SL_STATUS_OK;
  }

  if (ntok == 0U) {
    result->status = CLI_PARSE_EMPTY;
    return SL_STATUS_OK;
  }

  for (i = 0U; i < ntok; i++) {
    bool is_arg = false;
    const cli_node_t *next = match_child(level, level_count, tokens[i],
                                         modes, &is_arg);

    if (next == NULL) {
      // Nothing here matches. Distinguish a word that is simply wrong from a
      // command that was already complete before this word, because the two
      // deserve different messages.
      result->status = level_has_visible(level, level_count, modes)
                       ? CLI_PARSE_UNKNOWN : CLI_PARSE_TOO_MANY;
      result->bad_index = i;
      return SL_STATUS_OK;
    }

    if (is_arg) {
      if (result->arg_count >= (uint8_t)CLI_PARSER_MAX_ARGS) {
        result->status = CLI_PARSE_TOO_MANY;
        result->bad_index = i;
        return SL_STATUS_OK;
      }
      result->args[result->arg_count] = tokens[i];
      result->arg_count++;
    }

    node = next;
    level = node->children;
    level_count = node->child_count;
  }

  if (node->id == CLI_CMD_NONE) {
    result->status = CLI_PARSE_INCOMPLETE;
    return SL_STATUS_OK;
  }

  result->status = CLI_PARSE_OK;
  result->id = node->id;

  return SL_STATUS_OK;
}

sl_status_t cli_parser_help(const cli_node_t *root,
                            uint8_t root_count,
                            char *line,
                            uint8_t modes,
                            cli_help_fn_t emit,
                            void *user,
                            uint8_t *count)
{
  char *tokens[CLI_PARSER_MAX_TOKENS];
  uint8_t ntok;
  uint8_t walk;
  uint8_t i;
  bool too_many = false;
  bool ends_on_separator;
  const cli_node_t *level;
  uint8_t level_count;
  const cli_node_t *node = NULL;
  const char *prefix = "";
  size_t prefix_len;
  uint8_t emitted = 0U;
  uint16_t len;

  if ((root == NULL) || (line == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }

  if (count != NULL) {
    *count = 0U;
  }

  // Where the line ends decides what is on offer, so this has to be read
  // before tokenizing overwrites the separators.
  len = (uint16_t)strlen(line);
  ends_on_separator = ((len == 0U) || is_separator(line[len - 1U]));

  ntok = tokenize(line, tokens, (uint8_t)CLI_PARSER_MAX_TOKENS, &too_many);
  if (too_many) {
    return SL_STATUS_OK;
  }

  // A line ending part way through a word offers the keywords beginning with
  // it; that last word is a filter, not a step of the walk.
  if (ends_on_separator) {
    walk = ntok;
  } else {
    walk = (uint8_t)(ntok - 1U);
    prefix = tokens[ntok - 1U];
  }
  prefix_len = strlen(prefix);

  level = root;
  level_count = root_count;

  for (i = 0U; i < walk; i++) {
    bool is_arg = false;

    node = match_child(level, level_count, tokens[i], modes, &is_arg);
    if (node == NULL) {
      // The line so far names no command, so nothing can continue it.
      return SL_STATUS_OK;
    }

    level = node->children;
    level_count = node->child_count;
  }

  if (level == NULL) {
    // A leaf, or a malformed node claiming children it does not have.
    level_count = 0U;
  }

  for (i = 0U; i < level_count; i++) {
    const cli_node_t *child = &level[i];

    if (!is_visible(child, modes)) {
      continue;
    }

    // An argument accepts any word, so a partly typed one never rules it out.
    if ((child->kind == CLI_NODE_KEYWORD)
        && (strncmp(child->token, prefix, prefix_len) != 0)) {
      continue;
    }

    if (emit != NULL) {
      emit(child->token, child->help, user);
    }
    emitted++;
  }

  // Only worth saying the line is complete when it is complete as it stands.
  if ((prefix_len == 0U) && (node != NULL) && (node->id != CLI_CMD_NONE)) {
    if (emit != NULL) {
      emit(CLI_PARSER_CR_TOKEN, "", user);
    }
    emitted++;
  }

  if (count != NULL) {
    *count = emitted;
  }

  return SL_STATUS_OK;
}
