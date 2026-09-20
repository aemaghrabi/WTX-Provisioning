/***************************************************************************//**
 * @file
 * @brief Interactive console on the VCOM link, with Cisco IOS command syntax.
 *
 * Structure: cli_process() drives the XBee facade, collects whatever the
 * terminal has sent, and advances whatever request is in flight, in that order
 * and without waiting for any of them. Every command that has to talk to the
 * module becomes a request the state machine carries, never a loop.
 *
 * The grammar lives in one tree, @ref root, which the parser uses both to
 * dispatch a line and to answer '?'. Adding a command means adding a node and a
 * case, and its help follows automatically.
 *
 * Only one request is outstanding at a time, which is what the facade requires.
 * The walk behind "show xbee all" owns its own request while it runs, and the
 * console starts none of its own until the walk reports that it has finished.
 ******************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "sl_sleeptimer.h"

#include "app_log.h"
#include "console_uart.h"
#include "line_edit.h"

#include "cli_parser.h"
#include "xbee.h"
#include "xbee_at_table.h"
#include "xbee_dump_format.h"

#include "cli.h"
#include "cli_config.h"
#include "cli_show.h"
#include "cli_value.h"
#include "xbee_bridge.h"

/// @name Console modes, used as the parser's visibility mask
/// @{
#define M_USER  0x01U                      ///< User EXEC, "xbee> ".
#define M_PRIV  0x02U                      ///< Privileged EXEC, "xbee# ".
#define M_CONF  0x04U                      ///< Global configuration, "xbee(config)# ".
#define M_ALL   (M_USER | M_PRIV | M_CONF) ///< Everywhere.
/// @}

// Note on "show": IOS proper does not offer EXEC commands inside global
// configuration mode, and makes you write "do show ...". Here the whole show
// subtree is M_ALL instead, so that a parameter can be read back in the mode it
// was just written in. Setting a value and being unable to check it without
// leaving the mode is the wrong trade for a console whose entire job is reading
// and writing those values.

/// Ctrl-C, the only key that means anything while the console is busy.
#define CLI_CH_ETX  '\x03'

/// Column width of the keyword column in the help listing.
#define CLI_HELP_TOKEN_WIDTH  10

/// @name Command identifiers
///
/// Nothing may take CLI_CMD_NONE, which the parser reserves for a node that
/// only leads to others.
/// @{
enum {
  CMD_SHOW_AT = 1,
  CMD_SHOW_ALL,
  CMD_SHOW_INFO,
  CMD_SHOW_VERSION,
  CMD_ENABLE,
  CMD_DISABLE,
  CMD_EXIT,
  CMD_END,
  CMD_CONF_TERM,
  CMD_WRITE_MEM,
  CMD_RELOAD,
  CMD_LOG_VERBOSE,
  CMD_LOG_INFO,
  CMD_LOG_WARNING,
  CMD_LOG_ERROR,
  CMD_LOG_NONE,
  CMD_XBEE_AT_SET,
  CMD_NO_XBEE_AT,
  CMD_BRIDGE,
};
/// @}

/// Where the console is.
typedef enum {
  CLI_STATE_BRINGUP,   ///< Waiting for the facade to find the module.
  CLI_STATE_PROMPT,    ///< At a prompt, collecting a line.
  CLI_STATE_PASSWORD,  ///< At the password prompt, echo masked.
  CLI_STATE_GET,       ///< One read in flight.
  CLI_STATE_SET,       ///< One write in flight.
  CLI_STATE_EXEC,      ///< One action in flight, such as WR.
  CLI_STATE_WALK,      ///< "show xbee all" is running.
  CLI_STATE_BRIDGE,    ///< The bridge owns the link; the console is not here.
} cli_state_t;

// ---------------------------------------------------------------------------
// The command tree. Defined leaves first, because each parent names its
// children's array.
// ---------------------------------------------------------------------------

static const cli_node_t show_at_arg[] = {
  { "<NAME>", "Two-character AT command, for example CH", CLI_NODE_ARG,
    M_ALL, CMD_SHOW_AT, NULL, 0U },
};

static const cli_node_t show_xbee_children[] = {
  { "all", "Every readable AT parameter", CLI_NODE_KEYWORD,
    M_ALL, CMD_SHOW_ALL, NULL, 0U },
  { "at", "One AT parameter by name", CLI_NODE_KEYWORD,
    M_ALL, CLI_CMD_NONE, show_at_arg, 1U },
  { "info", "Module identity and serial mode", CLI_NODE_KEYWORD,
    M_ALL, CMD_SHOW_INFO, NULL, 0U },
};

static const cli_node_t show_children[] = {
  { "version", "Firmware build and module identity", CLI_NODE_KEYWORD,
    M_ALL, CMD_SHOW_VERSION, NULL, 0U },
  { "xbee", "XBee module information", CLI_NODE_KEYWORD,
    M_ALL, CLI_CMD_NONE, show_xbee_children, 3U },
};

static const cli_node_t configure_children[] = {
  { "terminal", "Configure from the terminal", CLI_NODE_KEYWORD,
    M_PRIV, CMD_CONF_TERM, NULL, 0U },
};

static const cli_node_t write_children[] = {
  { "memory", "Save the configuration to the module's flash", CLI_NODE_KEYWORD,
    M_PRIV, CMD_WRITE_MEM, NULL, 0U },
};

static const cli_node_t logging_level_children[] = {
  { "verbose", "Everything the XBee stack reports", CLI_NODE_KEYWORD,
    M_PRIV, CMD_LOG_VERBOSE, NULL, 0U },
  { "info", "Progress and results", CLI_NODE_KEYWORD,
    M_PRIV, CMD_LOG_INFO, NULL, 0U },
  { "warning", "Warnings and errors", CLI_NODE_KEYWORD,
    M_PRIV, CMD_LOG_WARNING, NULL, 0U },
  { "error", "Errors only", CLI_NODE_KEYWORD,
    M_PRIV, CMD_LOG_ERROR, NULL, 0U },
  { "none", "Nothing, so that logging cannot disturb the console",
    CLI_NODE_KEYWORD, M_PRIV, CMD_LOG_NONE, NULL, 0U },
};

static const cli_node_t logging_children[] = {
  { "level", "Lowest level the XBee stack may report", CLI_NODE_KEYWORD,
    M_PRIV, CLI_CMD_NONE, logging_level_children, 5U },
};

static const cli_node_t conf_at_value[] = {
  { "<VALUE>", "Hexadecimal, or text for a string parameter", CLI_NODE_ARG,
    M_CONF, CMD_XBEE_AT_SET, NULL, 0U },
};

static const cli_node_t conf_at_name[] = {
  { "<NAME>", "Two-character AT command, for example CH", CLI_NODE_ARG,
    M_CONF, CLI_CMD_NONE, conf_at_value, 1U },
};

static const cli_node_t conf_xbee_children[] = {
  { "at", "Set an AT parameter", CLI_NODE_KEYWORD,
    M_CONF, CLI_CMD_NONE, conf_at_name, 1U },
};

static const cli_node_t no_at_name[] = {
  { "<NAME>", "Two-character AT command, for example CH", CLI_NODE_ARG,
    M_CONF, CMD_NO_XBEE_AT, NULL, 0U },
};

static const cli_node_t no_xbee_children[] = {
  { "at", "Restore an AT parameter to its factory default", CLI_NODE_KEYWORD,
    M_CONF, CLI_CMD_NONE, no_at_name, 1U },
};

static const cli_node_t no_children[] = {
  { "xbee", "XBee module configuration", CLI_NODE_KEYWORD,
    M_CONF, CLI_CMD_NONE, no_xbee_children, 1U },
};

static const cli_node_t root[] = {
  { "bridge", "Connect this terminal straight to the XBee module",
    CLI_NODE_KEYWORD, M_PRIV, CMD_BRIDGE, NULL, 0U },
  { "configure", "Enter configuration mode", CLI_NODE_KEYWORD,
    M_PRIV, CLI_CMD_NONE, configure_children, 1U },
  { "disable", "Leave privileged commands", CLI_NODE_KEYWORD,
    M_PRIV, CMD_DISABLE, NULL, 0U },
  { "enable", "Turn on privileged commands", CLI_NODE_KEYWORD,
    M_USER, CMD_ENABLE, NULL, 0U },
  { "end", "Return to privileged EXEC", CLI_NODE_KEYWORD,
    M_CONF, CMD_END, NULL, 0U },
  { "exit", "Leave the current mode", CLI_NODE_KEYWORD,
    M_ALL, CMD_EXIT, NULL, 0U },
  { "logging", "Log output from the XBee stack", CLI_NODE_KEYWORD,
    M_PRIV, CLI_CMD_NONE, logging_children, 1U },
  { "no", "Undo a setting", CLI_NODE_KEYWORD,
    M_CONF, CLI_CMD_NONE, no_children, 1U },
  { "reload", "Reset the XBee module and bring it up again", CLI_NODE_KEYWORD,
    M_PRIV, CMD_RELOAD, NULL, 0U },
  { "show", "Show running system information", CLI_NODE_KEYWORD,
    M_ALL, CLI_CMD_NONE, show_children, 2U },
  { "write", "Save the running configuration", CLI_NODE_KEYWORD,
    M_PRIV, CLI_CMD_NONE, write_children, 1U },
  { "xbee", "XBee module configuration", CLI_NODE_KEYWORD,
    M_CONF, CLI_CMD_NONE, conf_xbee_children, 1U },
};

/// Number of top-level commands.
#define ROOT_COUNT  ((uint8_t)(sizeof(root) / sizeof(root[0])))

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

/// Where the console is.
static cli_state_t state = CLI_STATE_BRINGUP;

/// Which mode the operator is in, one of M_USER, M_PRIV or M_CONF.
static uint8_t mode = M_USER;

/// Storage for the line being typed.
static char line_storage[CLI_LINE_MAX];

/// Scratch copy for the parser, which tokenizes in place.
///
/// The typed line has to survive parsing: after '?' the operator carries on
/// from where they left off, and an error message points a caret at a word of
/// the line as it was written.
static char work[CLI_LINE_MAX];

/// Line discipline over line_storage.
static line_edit_t editor;

/// The one request the console itself has in flight.
static xbee_at_req_t req;

/// Command that request carries.
static uint16_t pending_command;

/// Value to be written, for CLI_STATE_SET.
static uint8_t pending_value[XBEE_AT_VALUE_MAX];

/// Length of that value.
static uint16_t pending_value_len;

/// True once the transport has accepted the request.
static bool pending_dispatched;

/// Tick by which the transport must have accepted it.
static uint32_t pending_deadline;

/// Password attempts used so far.
static uint8_t password_attempts;

/// Tick by which the password must have been typed.
static uint32_t password_deadline;

/// True once a parameter has been written and not yet saved with WR.
static bool config_dirty;

// ---------------------------------------------------------------------------
// Small helpers
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
 * Report whether the console is waiting on the module rather than the operator.
 ******************************************************************************/
static bool is_busy(void)
{
  return ((state == CLI_STATE_GET)
          || (state == CLI_STATE_SET)
          || (state == CLI_STATE_EXEC)
          || (state == CLI_STATE_WALK));
}

/***************************************************************************//**
 * The part of the prompt that says which mode the operator is in.
 ******************************************************************************/
static const char *prompt_suffix(void)
{
  const char *suffix;

  switch (mode) {
    case M_CONF: suffix = "(config)# "; break;
    case M_PRIV: suffix = "# "; break;
    default:     suffix = "> "; break;
  }

  return suffix;
}

/***************************************************************************//**
 * Write the prompt.
 ******************************************************************************/
static void print_prompt(void)
{
  (void)console_uart_printf("%s%s", CLI_HOSTNAME, prompt_suffix());
}

/***************************************************************************//**
 * Width of the prompt, for lining a caret up under the line above.
 ******************************************************************************/
static uint16_t prompt_width(void)
{
  return (uint16_t)(strlen(CLI_HOSTNAME) + strlen(prompt_suffix()));
}

/***************************************************************************//**
 * Return to an empty prompt.
 ******************************************************************************/
static void back_to_prompt(void)
{
  state = CLI_STATE_PROMPT;
  (void)line_edit_reset(&editor);
  print_prompt();
}

/***************************************************************************//**
 * Render a command's two characters into caller-provided storage.
 ******************************************************************************/
static const char *command_text(uint16_t command, char *text)
{
  if (xbee_at_id_to_str(command, text, 4U) != SL_STATUS_OK) {
    text[0] = '?';
    text[1] = '\0';
  }

  return text;
}

/***************************************************************************//**
 * Uppercase one ASCII letter.
 *
 * Not toupper(): that is locale dependent, and an AT command is ASCII by
 * definition. Characters that are not lowercase letters pass through, which is
 * what the punctuation commands such as "%V" and "R?" need.
 ******************************************************************************/
static char to_upper(char c)
{
  return ((c >= 'a') && (c <= 'z')) ? (char)((c - 'a') + 'A') : c;
}

/***************************************************************************//**
 * Turn a typed command name into a table identifier.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_INVALID_PARAMETER if the name is not two characters,
 *         SL_STATUS_NOT_FOUND if no such command exists.
 ******************************************************************************/
static sl_status_t name_to_id(const char *text, uint16_t *id)
{
  if (strlen(text) != 2U) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  *id = XBEE_AT_ID(to_upper(text[0]), to_upper(text[1]));

  if (xbee_at_table_find(*id) == NULL) {
    return SL_STATUS_NOT_FOUND;
  }

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Copy the typed line into the parser's scratch buffer.
 ******************************************************************************/
static void copy_line_to_work(void)
{
  const char *line = line_edit_line(&editor);

  (void)strncpy(work, line, sizeof(work) - 1U);
  work[sizeof(work) - 1U] = '\0';
}

/***************************************************************************//**
 * Offset of a word within the line, for the caret under a bad word.
 ******************************************************************************/
static uint16_t token_offset(const char *line, uint8_t index)
{
  uint16_t i = 0U;
  uint8_t seen = 0U;

  for (;;) {
    while ((line[i] == ' ') || (line[i] == '\t')) {
      i++;
    }
    if (line[i] == '\0') {
      break;
    }
    if (seen == index) {
      return i;
    }
    seen++;
    while ((line[i] != '\0') && (line[i] != ' ') && (line[i] != '\t')) {
      i++;
    }
  }

  return i;
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

/***************************************************************************//**
 * Point a caret at the word that was not understood, as IOS does.
 *
 * The line above has already been echoed and terminated, so the caret goes at
 * the same column on the line below it.
 ******************************************************************************/
static void report_caret(uint8_t token_index)
{
  uint16_t column = (uint16_t)(prompt_width()
                               + token_offset(line_edit_line(&editor),
                                              token_index));

  (void)console_uart_printf("%*s^\n", (int)column, "");
  (void)console_uart_puts("% Invalid input detected at '^' marker.\n");
}

/***************************************************************************//**
 * Say what the module, or the transport, made of a request.
 ******************************************************************************/
static void report_failure(const char *what, uint16_t command, sl_status_t status)
{
  char text[4];

  (void)console_uart_printf("%% %s %s failed, status 0x%04X\n",
                            what,
                            command_text(command, text),
                            (unsigned)status);
}

/***************************************************************************//**
 * Describe why a value was not accepted, quoting the documented bounds.
 ******************************************************************************/
static void report_value_error(const xbee_at_entry_t *entry,
                               const char *name,
                               sl_status_t status)
{
  switch (status) {
    case SL_STATUS_NOT_SUPPORTED:
      (void)console_uart_printf("%% %s is a%s %s and takes no value\n",
                                name,
                                (entry->type == XBEE_AT_TYPE_EXEC) ? "n" : "",
                                cli_value_type_name(entry->type));
      break;

    case SL_STATUS_WOULD_OVERFLOW:
      (void)console_uart_printf("%% Value is too wide for %s, which holds %u %s\n",
                                name,
                                (unsigned)entry->max_len,
                                (entry->type == XBEE_AT_TYPE_STRING)
                                ? "characters" : "bytes");
      break;

    case SL_STATUS_INVALID_RANGE:
      // Padded to the parameter's own width, so the bounds read the same way
      // the value itself does: 0x0B, not 0xB, for a one-byte parameter.
      (void)console_uart_printf("%% Value out of range for %s: 0x%0*lX to 0x%0*lX\n",
                                name,
                                (int)(entry->max_len * 2U),
                                (unsigned long)entry->min,
                                (int)(entry->max_len * 2U),
                                (unsigned long)entry->max);
      break;

    case SL_STATUS_PERMISSION:
      (void)console_uart_printf("%% %s cannot be written\n", name);
      break;

    case SL_STATUS_INVALID_PARAMETER:
    default:
      (void)console_uart_printf("%% Value is not valid for %s, expected %s\n",
                                name, cli_value_type_name(entry->type));
      break;
  }
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

/***************************************************************************//**
 * Print one help candidate.
 ******************************************************************************/
static void help_cb(const char *token, const char *help, void *user)
{
  (void)user;

  if (help[0] == '\0') {
    (void)console_uart_printf("  %s\n", token);
  } else {
    (void)console_uart_printf("  %-*s  %s\n",
                              CLI_HELP_TOKEN_WIDTH, token, help);
  }
}

/***************************************************************************//**
 * Answer '?' and put the operator back where they were.
 *
 * The line is untouched by the help request, so it is reprinted after the new
 * prompt and typing carries on from the same place.
 ******************************************************************************/
static void show_help(void)
{
  uint8_t count = 0U;

  (void)console_uart_puts("\n");

  copy_line_to_work();
  (void)cli_parser_help(root, ROOT_COUNT, work, mode, help_cb, NULL, &count);

  if (count == 0U) {
    (void)console_uart_puts("% Unrecognized command\n");
  }

  print_prompt();
  (void)console_uart_puts(line_edit_line(&editor));
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

/***************************************************************************//**
 * Arm a request and let the state machine carry it.
 ******************************************************************************/
static void start_request(cli_state_t next, uint16_t command)
{
  pending_command = command;
  pending_dispatched = false;
  pending_deadline = deadline_from_ms(CLI_REQUEST_TIMEOUT_MS);
  state = next;
}

/***************************************************************************//**
 * Read one parameter.
 *
 * @return true when a request was started.
 ******************************************************************************/
static bool cmd_show_at(const char *name)
{
  uint16_t id = 0U;
  sl_status_t status = name_to_id(name, &id);

  if (status == SL_STATUS_INVALID_PARAMETER) {
    (void)console_uart_printf("%% \"%s\" is not a two-character AT command\n",
                              name);
    return false;
  }
  if (status != SL_STATUS_OK) {
    (void)console_uart_printf("%% Unknown AT command \"%s\"\n", name);
    return false;
  }

  status = xbee_at_table_validate_get(id);
  if (status != SL_STATUS_OK) {
    const xbee_at_entry_t *entry = xbee_at_table_find(id);

    (void)console_uart_printf("%% %s (%s) has no value to read\n",
                              name, xbee_at_table_name(entry));
    return false;
  }

  start_request(CLI_STATE_GET, id);

  return true;
}

/***************************************************************************//**
 * Write one parameter, or restore it to its documented default.
 *
 * @param[in] name    Typed command name.
 * @param[in] text    Typed value, or NULL to use the factory default.
 *
 * @return true when a request was started.
 ******************************************************************************/
static bool cmd_set_at(const char *name, const char *text)
{
  const xbee_at_entry_t *entry;
  uint16_t id = 0U;
  sl_status_t status = name_to_id(name, &id);

  if (status == SL_STATUS_INVALID_PARAMETER) {
    (void)console_uart_printf("%% \"%s\" is not a two-character AT command\n",
                              name);
    return false;
  }
  if (status != SL_STATUS_OK) {
    (void)console_uart_printf("%% Unknown AT command \"%s\"\n", name);
    return false;
  }

  entry = xbee_at_table_find(id);

  if (text != NULL) {
    status = cli_value_parse(entry, text, pending_value,
                             (uint16_t)sizeof(pending_value),
                             &pending_value_len);
  } else {
    status = cli_value_default(entry, pending_value,
                               (uint16_t)sizeof(pending_value),
                               &pending_value_len);
    if (status == SL_STATUS_NOT_FOUND) {
      (void)console_uart_printf("%% The manual gives no default for %s\n", name);
      return false;
    }
  }

  if (status != SL_STATUS_OK) {
    report_value_error(entry, name, status);
    return false;
  }

  // The documented bounds are the table's business, not the parser's, so the
  // range is checked here rather than inside cli_value_parse(). Doing it before
  // the request is sent means a bad value never reaches the module, and the
  // operator is told the range instead of getting a bare ERROR back.
  status = xbee_at_table_validate_set(id, pending_value, pending_value_len);
  if (status != SL_STATUS_OK) {
    report_value_error(entry, name, status);
    return false;
  }

  start_request(CLI_STATE_SET, id);

  return true;
}

/***************************************************************************//**
 * Print the firmware and module identity.
 ******************************************************************************/
static void cmd_show_version(void)
{
  (void)console_uart_printf("xbee_provision console, built %s %s\n",
                            __DATE__, __TIME__);
  (void)console_uart_printf("AT command table: %u commands\n",
                            (unsigned)xbee_at_table_count());
  cli_show_info();
}

/***************************************************************************//**
 * Raise or lower what the XBee stack is allowed to log.
 ******************************************************************************/
static void cmd_logging_level(app_log_level_t level)
{
  sl_status_t status = app_log_set_level(level);

  if (status == SL_STATUS_INVALID_RANGE) {
    // The runtime level cannot go below the compile-time floor, because those
    // calls are not in the image at all.
    (void)console_uart_puts("% That level was compiled out. Rebuild with "
                            "APP_LOG_LEVEL_MIN=0 to use it.\n");
    return;
  }
  if (status != SL_STATUS_OK) {
    (void)console_uart_printf("%% Could not set the level, status 0x%04X\n",
                              (unsigned)status);
    return;
  }

  if (level != APP_LOG_LEVEL_NONE) {
    (void)console_uart_puts("% Log output shares this port, so it will "
                            "interleave with what you type.\n");
  }
}

/***************************************************************************//**
 * Leave configuration mode, saying so if anything is unsaved.
 ******************************************************************************/
static void leave_config(void)
{
  mode = M_PRIV;

  if (config_dirty) {
    (void)console_uart_puts("% Changes are in the module's running "
                            "configuration only. Use \"write memory\" to keep "
                            "them across a reset.\n");
  }
}

/***************************************************************************//**
 * Hand the link to the transparent bridge.
 *
 * @return true when the bridge took it, so the prompt waits for the escape
 *         sequence instead of being printed now.
 ******************************************************************************/
static bool cmd_bridge(void)
{
  static const xbee_bridge_params_t bridge_params = {
    .allow_escape = true,
    .quiesce_facade = true,
    // The facade already has the receive operations queued. Re-arming them
    // would abort the ones it is waiting on.
    .own_transport = false,
  };
  sl_status_t status;

  status = xbee_bridge_enter(&bridge_params);
  if (status != SL_STATUS_OK) {
    (void)console_uart_printf("%% Could not start the bridge, status 0x%04X\n",
                              (unsigned)status);
    return false;
  }

  (void)console_uart_puts("Bridging this terminal to the XBee module. "
                          "Everything you send now reaches it\n"
                          "unchanged, \"+++\" included.\n");
  // Control characters are 0x40 below the letter that names them, so the key
  // the operator has to press comes out of the configured character itself.
  (void)console_uart_printf("To come back: pause, press Ctrl-%c %u times, "
                            "then pause again.\n\n",
                            (char)('@' + (int)XBEE_BRIDGE_ESCAPE_CHAR),
                            (unsigned)XBEE_BRIDGE_ESCAPE_COUNT);

  state = CLI_STATE_BRIDGE;

  return true;
}

/***************************************************************************//**
 * Take the link back from the bridge and report what it carried.
 ******************************************************************************/
static void finish_bridge(void)
{
  xbee_bridge_stats_t bridge_stats;

  (void)xbee_bridge_leave();

  (void)console_uart_puts("\nBridge closed.\n");

  if (xbee_bridge_get_stats(&bridge_stats) == SL_STATUS_OK) {
    (void)console_uart_printf("%u bytes sent, %u received, %u dropped, "
                              "%u overruns\n",
                              (unsigned)bridge_stats.to_module_bytes,
                              (unsigned)bridge_stats.to_terminal_bytes,
                              (unsigned)bridge_stats.dropped_bytes,
                              (unsigned)bridge_stats.rx_overruns);
  }

  // Whatever was on the other end had the module to itself and may have
  // changed its serial mode, so what the console knows about it is no longer
  // something it can vouch for.
  (void)console_uart_puts("% The module may have been reconfigured. Use "
                          "\"reload\" to detect it again.\n");

  back_to_prompt();
}

/***************************************************************************//**
 * Carry out a matched command.
 *
 * @return true when a request was started, so the prompt must wait for it.
 *         False means the command is finished and the caller prints the prompt.
 ******************************************************************************/
static bool execute(const cli_parse_result_t *result)
{
  bool started = false;

  switch (result->id) {
    case CMD_SHOW_AT:
      started = cmd_show_at(result->args[0]);
      break;

    case CMD_SHOW_ALL:
      if (cli_show_all_start() == SL_STATUS_OK) {
        state = CLI_STATE_WALK;
        started = true;
      } else {
        (void)console_uart_puts("% A parameter walk is already running\n");
      }
      break;

    case CMD_SHOW_INFO:
      cli_show_info();
      break;

    case CMD_SHOW_VERSION:
      cmd_show_version();
      break;

    case CMD_ENABLE:
      password_attempts = 0U;
      password_deadline = deadline_from_ms(CLI_PASSWORD_TIMEOUT_MS);
      (void)line_edit_reset(&editor);
      (void)line_edit_set_echo(&editor, LINE_EDIT_ECHO_MASK);
      (void)console_uart_puts("Password: ");
      state = CLI_STATE_PASSWORD;
      started = true;
      break;

    case CMD_DISABLE:
      mode = M_USER;
      break;

    case CMD_EXIT:
      if (mode == M_CONF) {
        leave_config();
      } else if (mode == M_PRIV) {
        mode = M_USER;
      } else {
        // There is no session to end on a wired console, and pretending
        // otherwise would be a lie the next prompt immediately contradicts.
        (void)console_uart_puts("% Already at the top level.\n");
      }
      break;

    case CMD_END:
      leave_config();
      break;

    case CMD_CONF_TERM:
      mode = M_CONF;
      (void)console_uart_puts("Enter configuration commands, one per line. "
                              "End with \"end\".\n");
      break;

    case CMD_WRITE_MEM:
      (void)console_uart_puts("Building configuration...\n");
      start_request(CLI_STATE_EXEC, XBEE_AT_WR);
      started = true;
      break;

    case CMD_RELOAD:
      if (xbee_hw_reset() == SL_STATUS_OK) {
        (void)console_uart_puts("Reloading the module...\n");
        config_dirty = false;
        state = CLI_STATE_BRINGUP;
        started = true;
      } else {
        (void)console_uart_puts("% Could not reset the module\n");
      }
      break;

    case CMD_LOG_VERBOSE: cmd_logging_level(APP_LOG_LEVEL_VERBOSE); break;
    case CMD_LOG_INFO:    cmd_logging_level(APP_LOG_LEVEL_INFO); break;
    case CMD_LOG_WARNING: cmd_logging_level(APP_LOG_LEVEL_WARNING); break;
    case CMD_LOG_ERROR:   cmd_logging_level(APP_LOG_LEVEL_ERROR); break;
    case CMD_LOG_NONE:    cmd_logging_level(APP_LOG_LEVEL_NONE); break;

    case CMD_XBEE_AT_SET:
      started = cmd_set_at(result->args[0], result->args[1]);
      break;

    case CMD_NO_XBEE_AT:
      started = cmd_set_at(result->args[0], NULL);
      break;

    case CMD_BRIDGE:
      started = cmd_bridge();
      break;

    default:
      // Cannot happen: every identifier in the tree has a case above.
      (void)console_uart_puts("% Command is not implemented\n");
      break;
  }

  return started;
}

// ---------------------------------------------------------------------------
// Line handling
// ---------------------------------------------------------------------------

/***************************************************************************//**
 * Dispatch the line the operator has just finished.
 ******************************************************************************/
static void handle_line(void)
{
  cli_parse_result_t result;

  copy_line_to_work();

  if (cli_parser_parse(root, ROOT_COUNT, work, mode, &result) != SL_STATUS_OK) {
    back_to_prompt();
    return;
  }

  switch (result.status) {
    case CLI_PARSE_OK:
      if (execute(&result)) {
        // A request is in flight; the prompt comes back when it completes.
        (void)line_edit_reset(&editor);
        return;
      }
      break;

    case CLI_PARSE_EMPTY:
      break;

    case CLI_PARSE_INCOMPLETE:
      (void)console_uart_puts("% Incomplete command. Type '?' to see what "
                              "may follow.\n");
      break;

    case CLI_PARSE_UNKNOWN:
    case CLI_PARSE_TOO_MANY:
    default:
      report_caret(result.bad_index);
      break;
  }

  back_to_prompt();
}

/***************************************************************************//**
 * Check the password the operator has just typed.
 ******************************************************************************/
static void handle_password(void)
{
  // Not a constant-time comparison. The password guards against a mistake at
  // the terminal, not against an attacker: it is plain text in flash and
  // travels in the clear over the wire, so timing is not the weak part.
  bool accepted = (strcmp(line_edit_line(&editor), CLI_ENABLE_PASSWORD) == 0);

  (void)line_edit_reset(&editor);

  if (accepted) {
    (void)line_edit_set_echo(&editor, LINE_EDIT_ECHO_ON);
    mode = M_PRIV;
    back_to_prompt();
    return;
  }

  password_attempts++;

  if (password_attempts >= (uint8_t)CLI_PASSWORD_ATTEMPTS) {
    (void)line_edit_set_echo(&editor, LINE_EDIT_ECHO_ON);
    (void)console_uart_puts("% Bad passwords\n");
    back_to_prompt();
    return;
  }

  password_deadline = deadline_from_ms(CLI_PASSWORD_TIMEOUT_MS);
  (void)console_uart_puts("Password: ");
}

/***************************************************************************//**
 * Give up on the password prompt and go back to user EXEC.
 ******************************************************************************/
static void cancel_password(const char *reason)
{
  (void)line_edit_set_echo(&editor, LINE_EDIT_ECHO_ON);
  (void)console_uart_printf("%% %s\n", reason);
  back_to_prompt();
}

/***************************************************************************//**
 * Handle one received character.
 ******************************************************************************/
static void feed_byte(char c)
{
  line_edit_event_t event;

  if (state == CLI_STATE_BRINGUP) {
    // Nothing can be done with a command yet, and echoing it would scatter it
    // through the bring-up output. Drop it.
    return;
  }

  if (is_busy()) {
    // Only Ctrl-C means anything while the module is being talked to, and only
    // the parameter walk can be stopped: a read or a write has already handed
    // the transport a pointer to its request, so it has to be seen through.
    if (c == CLI_CH_ETX) {
      if (state == CLI_STATE_WALK) {
        cli_show_all_abort();
      }
    }
    return;
  }

  event = line_edit_feed(&editor, c);

  switch (event) {
    case LINE_EDIT_READY:
      if (state == CLI_STATE_PASSWORD) {
        handle_password();
      } else {
        handle_line();
      }
      break;

    case LINE_EDIT_HELP:
      show_help();
      break;

    case LINE_EDIT_ABORT:
      if (state == CLI_STATE_PASSWORD) {
        cancel_password("Cancelled");
      } else {
        print_prompt();
      }
      break;

    case LINE_EDIT_OVERFLOW:
      // The rest of the line is swallowed by the editor, so the prompt can be
      // written now rather than waiting for a terminator that reports nothing.
      (void)console_uart_printf("\n%% Line is longer than %u characters\n",
                                (unsigned)(CLI_LINE_MAX - 1U));
      print_prompt();
      break;

    case LINE_EDIT_NONE:
    default:
      break;
  }
}

/***************************************************************************//**
 * Collect whatever the terminal has sent.
 *
 * Called in every state, including the busy ones, so that the VCOM receive
 * buffer is emptied on every pass. It holds only
 * SL_IOSTREAM_EUSART_VCOM_RX_BUFFER_SIZE characters and the link has no flow
 * control, so anything left in it is at risk when the next characters arrive.
 ******************************************************************************/
static void service_input(void)
{
  char buf[CLI_READ_CHUNK];
  uint16_t count = 0U;
  uint16_t i;

  if (console_uart_read(buf, (uint16_t)sizeof(buf), &count) != SL_STATUS_OK) {
    return;
  }

  for (i = 0U; i < count; i++) {
    feed_byte(buf[i]);
  }
}

// ---------------------------------------------------------------------------
// Request completion
// ---------------------------------------------------------------------------

/***************************************************************************//**
 * Print the answer to a read.
 ******************************************************************************/
static void finish_get(void)
{
  const xbee_at_entry_t *entry = xbee_at_table_find(pending_command);
  char command[4];
  char text[XBEE_DUMP_VALUE_TEXT_CAP];

  if (req.result != SL_STATUS_OK) {
    report_failure("Reading", pending_command, req.result);
    return;
  }

  (void)console_uart_printf("%s (%s) = %s\n",
                            command_text(pending_command, command),
                            xbee_at_table_name(entry),
                            xbee_dump_format_value(entry, req.value,
                                                   req.value_len,
                                                   text, sizeof(text)));
}

/***************************************************************************//**
 * Report the outcome of a write.
 *
 * Silent on success, as IOS is: a configuration command that worked says
 * nothing. Only the failures are worth the operator's attention.
 ******************************************************************************/
static void finish_set(void)
{
  if (req.result != SL_STATUS_OK) {
    report_failure("Writing", pending_command, req.result);
    return;
  }

  config_dirty = true;
}

/***************************************************************************//**
 * Report the outcome of an action, which at present is only WR.
 ******************************************************************************/
static void finish_exec(void)
{
  if (req.result != SL_STATUS_OK) {
    report_failure("Command", pending_command, req.result);
    return;
  }

  (void)console_uart_puts("[OK]\n");
  config_dirty = false;
}

/***************************************************************************//**
 * Advance the request the console has in flight.
 ******************************************************************************/
static void advance_request(void)
{
  if (!pending_dispatched) {
    sl_status_t status;

    switch (state) {
      case CLI_STATE_GET:
        status = xbee_at_get(pending_command, &req);
        break;
      case CLI_STATE_SET:
        status = xbee_at_set(pending_command, pending_value,
                             pending_value_len, &req);
        break;
      case CLI_STATE_EXEC:
      default:
        status = xbee_at_exec_timeout(pending_command, &req,
                                      CLI_FLASH_TIMEOUT_MS);
        break;
    }

    if (status == SL_STATUS_OK) {
      pending_dispatched = true;
      return;
    }

    if (((status == SL_STATUS_NOT_READY) || (status == SL_STATUS_BUSY))
        && !tick_reached(pending_deadline)) {
      // The transport may be re-entering Command mode underneath. Try again on
      // the next pass, within CLI_REQUEST_TIMEOUT_MS.
      return;
    }

    report_failure("Sending", pending_command, status);
    back_to_prompt();
    return;
  }

  if (!xbee_at_req_complete(&req)) {
    return;
  }

  switch (state) {
    case CLI_STATE_GET: finish_get(); break;
    case CLI_STATE_SET: finish_set(); break;
    case CLI_STATE_EXEC:
    default:            finish_exec(); break;
  }

  back_to_prompt();
}

/***************************************************************************//**
 * Wait for bring-up, then open the console.
 ******************************************************************************/
static void advance_bringup(void)
{
  xbee_state_t xbee = xbee_get_state();

  if (xbee == XBEE_STATE_READY) {
    (void)console_uart_puts("\nXBee console ready. Type '?' for help.\n\n");
    back_to_prompt();
    return;
  }

  if (xbee == XBEE_STATE_FAILED) {
    (void)console_uart_printf("\n%% XBee bring-up failed, status 0x%04X\n",
                              (unsigned)xbee_get_result());
    (void)console_uart_puts("% The console is open, but the module will not "
                            "answer. Try \"enable\" then \"reload\".\n\n");
    back_to_prompt();
    return;
  }
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

/***************************************************************************//**
 * Echo sink for the line editor.
 ******************************************************************************/
static void echo_cb(const char *text, uint16_t len, void *user)
{
  (void)user;

  (void)console_uart_write(text, len);
}

sl_status_t cli_init(void)
{
  static const xbee_config_t config = {
    .mode = XBEE_MODE_AUTO,
    .power_cycle = (CLI_POWER_CYCLE != 0),
    .settle_ms = 0U,         // use the configured default
    .probe_timeout_ms = 0U,  // use the configured default
  };
  sl_status_t status;

  status = console_uart_init();
  if (status != SL_STATUS_OK) {
    return status;
  }

  // The stack logs to this same stream, so a log line would land in the middle
  // of whatever the operator is typing. Start quiet; "logging level" turns it
  // back on when something needs diagnosing.
  (void)app_log_set_level(APP_LOG_LEVEL_NONE);

  status = line_edit_init(&editor, line_storage, (uint16_t)sizeof(line_storage),
                          echo_cb, NULL);
  if (status != SL_STATUS_OK) {
    return status;
  }

  state = CLI_STATE_BRINGUP;
  mode = M_USER;
  config_dirty = false;

  (void)console_uart_puts("\nStarting the XBee module...\n");

  return xbee_init(&config);
}

void cli_process(void)
{
  if (state == CLI_STATE_BRIDGE) {
    // Nothing else runs while the bridge holds the link. The facade is not
    // driven, because it and the bridge would drain the same receive ring, and
    // the terminal is not read here, because every byte on it belongs to the
    // module now.
    xbee_bridge_process();

    if (!xbee_bridge_is_active()) {
      finish_bridge();
    }

    return;
  }

  // The facade first: it owns the transports, and everything below depends on
  // it having been given a chance to move.
  (void)xbee_process();

  service_input();

  switch (state) {
    case CLI_STATE_BRINGUP:
      advance_bringup();
      break;

    case CLI_STATE_GET:
    case CLI_STATE_SET:
    case CLI_STATE_EXEC:
      advance_request();
      break;

    case CLI_STATE_WALK:
      if (!cli_show_all_process()) {
        back_to_prompt();
      }
      break;

    case CLI_STATE_PASSWORD:
      if (tick_reached(password_deadline)) {
        cancel_password("Timed out waiting for the password");
      }
      break;

    case CLI_STATE_PROMPT:
    default:
      break;
  }
}
